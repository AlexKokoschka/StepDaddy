// Reference voice for tests/delay.c: the same voice as voice/src/voice303.c
// with the earlier delay implementation, so the delay can be checked against
// an otherwise identical voice, sample for sample. Keep its filter in step
// with voice303.c.
#include "voice303.h"
#include "voice_profile.h"
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Fixed-point helpers. Audio is Q28: 1.0 = 1 << 28, leaving 3 bits of headroom.
// ---------------------------------------------------------------------------

#define ONE_Q28   (1 << 28)
#define Q28(x)    ((int32_t)((x) * 268435456.0f))
#define Q30(x)    ((int32_t)((x) * 1073741824.0f))
#define Q24(x)    ((int32_t)((x) * 16777216.0f))

// (a * b) >> shift without a 64-bit multiply. The Cortex-M0+ has a fast 32x32->32
// multiplier but no 64-bit one, so the compiler would call a slow library routine.
// Built from four 16x16 products; the result is within a few LSBs of exact.
static inline int32_t mul_shift(int32_t a, int32_t b, int shift) {
  int32_t  ah = a >> 16, bh = b >> 16;
  uint32_t al = (uint32_t)a & 0xFFFFu, bl = (uint32_t)b & 0xFFFFu;
  int32_t  hh = ah * bh;
  int32_t  hl = ah * (int32_t)bl;
  int32_t  lh = (int32_t)al * bh;
  uint32_t ll = al * bl;
  return (int32_t)((uint32_t)hh << (32 - shift)) + (hl >> (shift - 16)) + (lh >> (shift - 16)) + (int32_t)(ll >> shift);
}
static inline int32_t mul_q28(int32_t a, int32_t b) { return mul_shift(a, b, 28); }
static inline int32_t mul_q30(int32_t a, int32_t b) { return mul_shift(a, b, 30); }

// Soft clipper: 1.5x - 0.5x^3 inside +-1, flat outside. Smooth, cheap, bounded.
static inline int32_t softclip(int32_t x) {
  if (x >= ONE_Q28)  return ONE_Q28;
  if (x <= -ONE_Q28) return -ONE_Q28;
  int32_t x2 = mul_q28(x, x);
  return mul_q28(x, Q28(1.5f) - (x2 >> 1));
}

// Rational (Pade) tanh approximation for the soft clipping.
// LUTs are generated from the equation at startup.
#define CURVE_STEPS 512
static int32_t rational_tab[CURVE_STEPS + 1];
static int32_t sine_tab[CURVE_STEPS + 1];
static void curves_init(void) {
  for (int i=0; i<=CURVE_STEPS; ++i) {
    float x=-4.0f+8.0f*i/CURVE_STEPS;
    float y=x<=-3.0f ? -1.0f : x>=3.0f ? 1.0f
            : x*(27.0f+x*x)/(27.0f+9.0f*x*x);
    rational_tab[i]=Q28(y);
    sine_tab[i]=i==CURVE_STEPS ? 0 : Q28(sinf(6.28318530718f*i/CURVE_STEPS));
  }
}
static inline int32_t rational_q28(int32_t x) {
  if (x>=3*ONE_Q28) return ONE_Q28;
  if (x<=-3*ONE_Q28) return -ONE_Q28;
  uint32_t u=(uint32_t)(x+4*ONE_Q28), i=u>>22;
  int32_t frac=(int32_t)((u>>7)&0x7fff);
  int32_t a=rational_tab[i], delta=rational_tab[i+1]-a;
  return a+(((delta>>7)*frac)>>8);
}
static inline int32_t rational_q22(int32_t x) {
  if (x>=3*(1<<22)) return ONE_Q28;
  if (x<=-3*(1<<22)) return -ONE_Q28;
  return rational_q28(x*64);
}
// Q22 radians -> wrapped Q22 cycles -> interpolated sine. Full input range
// is bounded by the input clamp and bounded drive before reaching here.
static inline int32_t sine_q22(int32_t x) {
  int32_t cycles=mul_q30(x,170891319); // 1/(2*pi) in Q30
  uint32_t u=(uint32_t)cycles&((1u<<22)-1u), i=u>>13;
  int32_t frac=(int32_t)((u&8191u)<<3);
  int32_t a=sine_tab[i], delta=sine_tab[i+1]-a;
  return a+(((delta>>8)*frac)>>8);
}

// Output limiter: unity gain up to 0.7, then a smooth knee reaching 1.0 (0 dBFS).
static inline int32_t limit(int32_t x) {
  const int32_t T = Q28(0.7f), W = Q28(0.6f);
  int32_t a = x < 0 ? -x : x;
  if (a <= T) return x;
  int32_t e = a - T;
  int32_t y = e >= W ? ONE_Q28 : T + e - mul_q28(mul_q28(e, e), Q28(1.0f / 1.2f));
  return x < 0 ? -y : y;
}

// ---------------------------------------------------------------------------
// Oscillator: PolyBLEP band-limited saw; square = difference of two saws.
// ---------------------------------------------------------------------------

static inline int32_t saw_blep(uint32_t phase, uint32_t inc) {
  int32_t t  = (int32_t)(phase >> 4);          // 0..1 in Q28
  int32_t dt = (int32_t)(inc >> 4);
  int32_t y  = (t << 1) - ONE_Q28;             // naive saw, -1..1
  int32_t div = dt >> 9;
  if (div <= 0) return y;                      // sub-audio: no correction needed

  if (t < dt) {                                // just after the wrap
    int32_t x = (int32_t)(((uint32_t)t << 6) / (uint32_t)div);          // t/dt, Q15
    int32_t b = 2 * x - ((x * x) >> 15) - 32768;
    y -= b * 8192;
  } else if (t > ONE_Q28 - dt) {               // just before the wrap
    int32_t x = (int32_t)(((uint32_t)(ONE_Q28 - t) << 6) / (uint32_t)div); // (1-t)/dt, Q15
    int32_t b = ((x * x) >> 15) - 2 * x + 32768;
    y -= b * 8192;
  }
  return y;
}

// Distort amount is a wet/dry blend. Drive is a separate panel control
// (startup default in voice_profile.h); mode changes crossfade without resetting
// filter/envelope state. Chords reduce drive as a Yummy extension.
static inline int32_t shape_sample(voice_t *v, int32_t x) {
  const int32_t bound=2*ONE_Q28;
  if (x>bound) x=bound;
  if (x<-bound) x=-bound;
  int32_t a=0,b=0;
  if (v->dist_mode!=ONE_Q28) {
    int32_t u=mul_shift(x,v->soft_gain,22); // Q28 * Q16 -> Q22
    a=mul_q28(rational_q22(u),v->dist_makeup);
  }
  if (v->dist_mode!=0) {
    int32_t u=mul_shift(x,v->sine_gain,22);
    b=mul_q28(sine_q22(u),Q28(0.7f));
  }
  return mul_q28(a,ONE_Q28-v->dist_mode)+mul_q28(b,v->dist_mode);
}
static inline int32_t distortion(voice_t *v, int32_t clean) {
  int32_t mid=(v->dist_prev>>1)+(clean>>1);
  v->dist_prev=clean;
  int32_t y=(shape_sample(v,mid)>>1)+(shape_sample(v,clean)>>1);
  v->dist_dc+=mul_q30(y-v->dist_dc,v->g_dc);
  y-=v->dist_dc;
  // Conservative anti-alias and tone filtering approximation.
  v->tone_lp+=mul_q30(y-v->tone_lp,v->g_tone);
  return mul_q28(v->tone_lp,v->dist_wet)+mul_q28(clean,ONE_Q28-v->dist_wet);
}

// Delay: tempo-synced, steady timing, with tape character in the feedback:
// each repeat a little darker (low-pass) and lighter in the bass (high-pass),
// and gently saturated, so long tails build up without running away.
const char *const voice_delay_div_names[VOICE_DELAY_DIVS] = { "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4" };
static const float DELAY_BEATS[VOICE_DELAY_DIVS] = { 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f };

int voice_delay_div(float t) {
  int i = (int)(t * VOICE_DELAY_DIVS);
  return i < 0 ? 0 : (i >= VOICE_DELAY_DIVS ? VOICE_DELAY_DIVS - 1 : i);
}

static void delay_target(voice_t *v) {
  float bpm = v->bpm < 20.0f ? 20.0f : v->bpm;
  float samples = 60.0f / bpm * DELAY_BEATS[voice_delay_div(v->p.delay_time)] * VOICE_FS;
  if (samples > VOICE_DELAY_LEN - 4) samples = VOICE_DELAY_LEN - 4;
  v->dl_target = (int32_t)(samples * 256.0f);
  if (v->dl_d == 0) v->dl_d = v->dl_target;
}

static inline int32_t delay(voice_t *v, int32_t in) {
  // glide towards a new delay time (tempo or division change) like tape speeding up
  int32_t diff = v->dl_target - v->dl_d;
  if (diff > 32) v->dl_d += 32; else if (diff < -32) v->dl_d -= 32; else v->dl_d = v->dl_target;
  // read with linear interpolation
  uint32_t di = (uint32_t)v->dl_d >> 8, fr = (uint32_t)v->dl_d & 0xFF;
  uint32_t r0 = v->dl_w + VOICE_DELAY_LEN - di, r1 = r0 + VOICE_DELAY_LEN - 1;
  r0 %= VOICE_DELAY_LEN; r1 %= VOICE_DELAY_LEN;
  int32_t a = v->dl_buf[r0], b = v->dl_buf[r1];
  int32_t echo = (a + (((b - a) * (int32_t)fr) >> 8)) * 8192;   // Q15 -> Q28
  // feedback: darker, less bass, saturated
  v->dl_lp += mul_q30(echo - v->dl_lp, v->g_dl_lp);
  v->dl_hp += mul_q30(v->dl_lp - v->dl_hp, v->g_dl_hp);
  int32_t fb = softclip(mul_q28(v->dl_lp - v->dl_hp, v->dl_fb));
  int32_t w = (in + fb) >> 13;
  v->dl_buf[v->dl_w] = (int16_t)(w > 32767 ? 32767 : (w < -32768 ? -32768 : w));
  if (++v->dl_w >= VOICE_DELAY_LEN) v->dl_w = 0;
  return in + mul_q28(echo, v->dl_wet);
}

static inline int32_t oscillator(voice_osc_t *o, bool square) {
  int32_t s = saw_blep(o->phase, o->phase_inc);
  if (square) s = (s - saw_blep(o->phase + 0x80000000u, o->phase_inc)) >> 1;
  o->phase += o->phase_inc;
  return s;
}

// Chord level compensation is a Yummy-only extension. Accent now reaches
// the nonlinear stage before distortion for both mono and paraphonic notes.
#define TWO_VT   Q28(0.624f)        // 2 x thermal voltage (0.312) of the model
static const float MIX_LEVEL[VOICE_MAX_NOTES + 1]   = { 0.0f, 1.0f, 0.5f, 0.3333f, 0.25f };
static const float CHORD_DRIVE[VOICE_MAX_NOTES + 1] = {0,1,0.60f,0.42f,0.32f};
static const float CHORD_MAKEUP[VOICE_MAX_NOTES + 1] = { 0.0f, 1.0f, 1.41421356f, 1.73205081f, 2.0f };

// ---------------------------------------------------------------------------
// Parameter mapping (control rate, float)
// ---------------------------------------------------------------------------

static const float T_BLOCK = (float)VOICE_BLOCK / VOICE_FS;
static const float FS_FILTER = (float)VOICE_FS;   // 1.0.0: ZDF core, no oversampling

// Fast exponentials. The C library's expf/exp2f are very slow in software
// floating point on the M0+. Small arguments (envelope rates, low filter
// coefficients) use a Taylor series, which is accurate to float precision
// there; larger ones use a quartic for 2^frac plus an exponent shift (~5e-6).
static inline float exp_series(float x) {     // |x| < 0.125
  return 1.0f + x * (1.0f + x * (0.5f + x * (0.16666667f + x * 0.041666667f)));
}

static float fast_exp2(float x) {
  if (x > -0.18f && x < 0.18f) return exp_series(x * 0.69314718f);
  if (x < -60.0f) return 0.0f;
  if (x > 60.0f) x = 60.0f;
  int i = (int)x;
  if (x < (float)i) i--;
  float f = x - (float)i;
  float y = 1.0f + f * (0.69315308f + f * (0.24015361f + f * (0.05581632f + f * 0.01353255f)));
  union { float f; int32_t i; } u = { y };
  u.i += (int32_t)((uint32_t)i << 23);
  return u.f;
}

static float fast_exp(float x) {
  if (x > -0.125f && x < 0.125f) return exp_series(x);
  return fast_exp2(x * 1.4426950f);
}

// One-pole coefficient 1 - exp(-2*pi*f/fs), without cancellation.
static float coef_onepole(float hz, float fs) {
  float x = 6.2831853f * hz / fs;
  if (x < 0.125f) return x * (1.0f - x * (0.5f - x * (0.16666667f - x * 0.041666667f)));
  return 1.0f - fast_exp(-x);
}

// Everything that only depends on the knobs: recomputed when they change.
static void derive_params(voice_t *v) {
  const voice_params_t *p = &v->p;

  v->d.decay_shape=sqrtf(p->decay);
  v->d.tune_semis = (p->tune - 0.5f) * 14.0f;                      // +-7 semitones
  // Knob-derived DSP is smoothed and mapped in control_update. These
  // coefficient arrays depend only on the chosen Drive and chord size.
  float u=p->dist_drive;
  if (!(u>=0.0f)) u=0.0f;
  if (u>1.0f) u=1.0f;
  float g=0.05f+99.95f*u;
  v->od_gain=(int32_t)(g*65536.0f);
  // At very low Drive the exact compensation can exceed Q28 headroom.
  // Keep a bounded factor; default Drive=.5 is below this cap.
  float makeup=2.0f*(1.0f+8.0f*u)/g;
  if (makeup>2.0f) makeup=2.0f;
  v->dist_makeup_target=Q28(makeup);
  for (int n=1;n<=VOICE_MAX_NOTES;n++) {
    float nscale=CHORD_DRIVE[n];
    v->od_gain_n[n]=(int32_t)(g*nscale*65536.0f);
    v->od_makeup_n[n]=ONE_Q28; // compensation remains in post_gain
  }
  v->drive=Q24(1.0f+9.0f*u); // sine path's Drive
  v->g_tone=Q30(coef_onepole(10000.0f,VOICE_FS));

  // Delay: one knob for echo level and feedback together
  float a = p->delay_amount;
  // Turned off, it keeps running for a second with no echo and no feedback,
  // which flushes old echoes; turned on again within that time it just carries
  // on (recorded movements may dip to 0 and back), otherwise the memory is cleared.
  if (a > 0.005f) {
    if (!v->dl_on) { memset(v->dl_buf, 0, sizeof(v->dl_buf)); v->dl_lp = v->dl_hp = 0; }
    v->dl_on = true;
    v->dl_off = 0;
    v->dl_wet = Q28(0.55f * a);
    v->dl_fb = Q28((0.2f + 0.65f * a) * (2.0f / 3.0f));   // the soft clipper adds 1.5x: overall feedback 0.2 .. 0.85
  } else {
    if (v->dl_on && v->dl_off == 0) v->dl_off = VOICE_DELAY_LEN;
    v->dl_wet = 0;
    v->dl_fb = 0;
  }
  delay_target(v);
}

// Once per block: envelopes, glide, cutoff.
static void control_update(voice_t *v) {
  // --- pitch and glide (~60 ms RC slide, like the original), per oscillator ---
  int32_t mix = Q28(MIX_LEVEL[v->n_notes]);
  for (int i = 0; i < VOICE_MAX_NOTES; i++) {
    voice_osc_t *o = &v->osc[i];
    int32_t g_target = o->active ? mix : 0;
    int32_t diff = g_target - o->gain;
    if (diff > -VOICE_BLOCK * 4 && diff < VOICE_BLOCK * 4) o->gain = g_target;   // close enough: land on it
    o->gain_step = (g_target - o->gain) / VOICE_BLOCK;
    if (!o->active && o->gain == 0) continue;           // silent: skip
    if (o->gliding) {
      o->pitch += (o->pitch_target - o->pitch) * v->c.glide_coef;
      if (fabsf(o->pitch_target - o->pitch) < 0.001f) { o->pitch = o->pitch_target; o->gliding = false; }
    } else {
      o->pitch = o->pitch_target;
    }
    float semis = o->pitch + v->d.tune_semis;
    if (semis != o->last_semis) {
      o->last_semis = semis;
      float hz = 440.0f * fast_exp2((semis - 69.0f) * (1.0f / 12.0f));
      o->target_inc = (uint32_t)(hz * (4294967296.0f / VOICE_FS));
    }
    o->phase_inc_step = ((int32_t)(o->target_inc - o->phase_inc)) / VOICE_BLOCK;
  }

  // Smooth each control in its own domain: a sample-wise .005 coefficient at
  // 44.1 kHz (fitted to measured knob response), aggregated over this block.
  const float targets[6]={v->p.cutoff,v->p.resonance,v->p.envmod,
                         v->d.decay_shape,0.9801f*v->p.accent,v->p.distortion};
  for (int i=0;i<6;i++) {
    v->knob[i]+=(targets[i]-v->knob[i])*v->profile.knob_coef;
    if (fabsf(targets[i]-v->knob[i])<0.00001f) v->knob[i]=targets[i];
  }
  v->mode_smooth+=((float)v->p.dist_type-v->mode_smooth)*v->profile.knob_coef;
  if (fabsf((float)v->p.dist_type-v->mode_smooth)<0.00001f)
    v->mode_smooth=(float)v->p.dist_type;
  float c=v->knob[0],r=v->knob[1],env=v->knob[2],acc=v->knob[4];

  // Decay coefficient fitted to audio measurements, converted to block rate.
  float rate=voice_profile_decay_rate(r,v->knob[3],v->accented);
  // log(1-rate) through fourth order; rate <= .00029.
  float log_keep=-rate*(1.0f+rate*(0.5f+rate*(1.0f/3.0f+rate*0.25f)));
  v->meg*=fast_exp(log_keep*44100.0f*T_BLOCK);
  if (v->meg<1e-12f) v->meg=0;

  // Retain Yummy's amplifier timing in this pass.
  if (v->gate) {
    if (v->vca_attack) {
      v->vca+=v->c.vca_attack_step;
      if (v->vca>=1) {v->vca=1;v->vca_attack=false;}
    } else v->vca*=v->c.vca_hold_mul;
  } else v->vca*=v->c.vca_release_mul;

  // Persistent charge, follower and amplitude states approximate a coupled
  // accent envelope network. Never clear the states on every note.
  float target=v->accented ? v->meg : 0.0f;
  float charge=1.0f-fast_exp(-T_BLOCK/(0.008f+0.060f*r));
  float release=1.0f-fast_exp(-T_BLOCK/(0.040f+0.160f*r));
  v->acc_charge+=(target-v->acc_charge)*(target>v->acc_charge ? charge : release);
  v->acc_sweep+=(v->acc_charge-v->acc_sweep)*v->profile.acc_follow;
  float excitation=acc*v->acc_charge;
  float amp_target=excitation>0 ? sqrtf(excitation*sqrtf(excitation)) : 0;
  v->acc_vca+=(amp_target-v->acc_vca)*v->profile.acc_amp;

  voice_profile_mod_t mod=voice_profile_mod(c,env);
  float control=mod.scale*fast_exp(0.5f+2.0f*mod.depth*(v->meg-0.435f)
                                          +1.5f*acc*v->acc_sweep);
  control*=1.15f; // neutral VCF trim; pre-filter scaling fitted to measurements
  if (control>1.4f) control=1.4f;
  // Measured cutoff law: .014*exp(4*control), scaled by 1.4, then converted
  // to Hz for the filter below (the conversion is approximate).
  float h=0.0196f*fast_exp(4.0f*control);
  if (h>1.96f) h=1.96f;
  // 1.0.0: the zero-delay-feedback core resonates exactly at its cutoff, so
  // the Hz scale is refitted to the emulation's resonant peaks (1.042x).
  float fc=h*(1.0420f*44100.0f/6.28318530718f);
  // The ZDF core stays in tune and stable up the range (0.3.6 stopped at
  // 11 kHz: its one-sample feedback delay rang at about 12 kHz above that).
  float ceiling=VOICE_FS*0.375f;
  if (ceiling>18000) ceiling=18000;
  if (fc>ceiling) fc=ceiling;
  if (fc<20) fc=20;
  v->last_cutoff_hz=fc;
  float xw=3.14159265f*fc/FS_FILTER;
  // Listening refinement for this approximate ladder: compress only the
  // upper 30% and damp the bright/accented peak progressively there.
  // Continuous slope at .7 avoids an audible boundary while turning.
  float top=r>0.7f ? (r-0.7f)*(1.0f/0.3f) : 0.0f;
  float taper=top*top;
  // 0.3.6: Resonance knob curve r^1.5 (same at 0 and maximum). Matched to the
  // emulation at Cutoff 50%: its resonance stays broad up to about 75% and
  // only turns sharp near the top of the knob.
  float feedback=r*sqrtf(r)-0.08f*taper;
  float damping=1.0f-taper*(0.12f*fc/(fc+3000.0f)
                                      +0.06f*acc*v->acc_charge);
  // 1.0.0 resonance voicing for the ZDF core, refitted to the same 303
  // emulation. Without the one-sample delay the loop needs more gain for the
  // same peak (4.60 + 0.79r^2; the saturating stages keep it from
  // running away). The 0.3.6 taper above 5.4 kHz is gone: the old core's
  // whistle up there came from its delay, and the emulation keeps its
  // resonance up to the top. Below about 1.2 kHz a little feedback is taken
  // away (at most 21%), as the emulation's resonance is gentler down there.
  {float lo=1228.0f/fc;lo*=lo;damping*=1.0f-0.207f*lo/(1.0f+lo);}
  float k_full=feedback*(4.6f+0.793f*r*r);
  float k_now=k_full*damping;
  {
    // g = tan(xw) via the half angle: t = tan(xw/2) by Pade (3,2), error under
    // 0.02% up to xw = 1.2 (18 kHz even without oversampling at 48 kHz).
    float hw=0.5f*xw,hw2=hw*hw;
    float t=hw*(15.0f-hw2)/(15.0f-6.0f*hw2);
    // G = g/(1+g) with g = 2t/(1-t^2), in one division.
    float G=2.0f*t/(1.0f+2.0f*t-t*t),H=1.0f-G,G2=G*G;
    float c=1.0f-(float)v->g_hp*(1.0f/1073741824.0f);   // explicit feedback high-pass
    float kc=k_now*c;
    int32_t a_new[4]={Q30(G2*G*H),Q30(G2*H),Q30(G*H),Q30(H)};
    for(int i=0;i<4;i++)v->za_step[i]=(a_new[i]-v->za[i])/VOICE_BLOCK;
    v->zG_step=(Q30(G)-v->zG)/VOICE_BLOCK;
    v->zkc_step=(Q28(kc)-v->zkc)/VOICE_BLOCK;
    v->zr_step=(Q30(1.0f/(1.0f+kc*G2*G2))-v->zr)/VOICE_BLOCK;
  }

  // Accent boosts the signal before distortion. Resonance makeup follows
  // a measured 1.4-.4*R scalar; final output scaling is Yummy-specific.
  v->d.acc_gain=1.0f+0.9f*v->acc_vca;
  v->d.level=(1.4f-0.4f*r)*v->p.volume*1.125f;
  // 0.3.6 level, fitted to the same emulation (constants refitted in 1.0.0). Feedback costs passband
  // level, so where the taper removes it (high cutoff) the voice got about 6 dB
  // louder; it now keeps its level, and like the emulation it gets louder as
  // Cutoff closes (+5 dB at 50%, +7 dB at 0, Resonance at maximum). Both scale
  // with Resonance, so low Resonance is unchanged.
  float u=1.0f-c; u=sqrtf(sqrtf(u>0?u:0));
  // Turning Resonance up lost about 9 dB at Cutoff 50% (filter feedback costs
  // passband level); the emulation loses 5.4 dB (3.4 at 25%, 5.4 at 50%, 6.0 at
  // 75%). 3.6r^2 dB of makeup matches it within 0.8 dB at every step.
  float res_makeup=3.371f*r*r;
  // With Cutoff fully open the emulation loses its level earlier on the knob
  // (-10 dB already at 50% Resonance); c^4 confines this to high Cutoff.
  {float c2=c*c,q=1.0f-r; res_makeup-=21.34f*c2*c2*r*q*sqrtf(q);}
  float level_fit=(1.0f+0.6f*k_now)/(1.0f+0.6f*k_full)*fast_exp((4.656f*r*r*u+res_makeup)*0.11512925f);
  float gain=v->vca*v->d.acc_gain*v->d.level*level_fit;
  v->out_gain_step=(Q28(gain)-v->out_gain)/VOICE_BLOCK;
  int n=v->n_notes<1 ? 1 : v->n_notes;
  // Sine shaping is near-linear for quiet chords: restore its lost level
  // after shaping, without driving the shared nonlinear stage harder.
  const float sine_makeup[5]={0,1,1.1785113f,1.3746435f,1.5625f};
  float compensation=1.0f+v->knob[5]*v->mode_smooth*(sine_makeup[n]-1.0f);
  int32_t post=Q28(CHORD_MAKEUP[n]*compensation);
  v->post_gain_step=(post-v->post_gain)/VOICE_BLOCK;
  if (!v->post_gain_step) v->post_gain=post;

  int32_t wet=Q28(v->knob[5]), mode=Q28(v->mode_smooth);
  if (wet-v->dist_wet<VOICE_BLOCK && wet-v->dist_wet>-VOICE_BLOCK)
    v->dist_wet=wet;
  if (mode-v->dist_mode<VOICE_BLOCK && mode-v->dist_mode>-VOICE_BLOCK)
    v->dist_mode=mode;
  v->dist_wet_step=(wet-v->dist_wet)/VOICE_BLOCK;
  v->dist_mode_step=(mode-v->dist_mode)/VOICE_BLOCK;
  v->dist_makeup_step=(v->dist_makeup_target-v->dist_makeup)/VOICE_BLOCK;
  v->soft_gain_step=(v->od_gain_n[n]-v->soft_gain)/VOICE_BLOCK;
  float nscale=CHORD_DRIVE[n];
  int32_t sg=(int32_t)((float)v->drive*(1.0f/256.0f)*nscale); // Q24 -> Q16
  v->sine_gain_step=(sg-v->sine_gain)/VOICE_BLOCK;
  v->dist_on=v->p.distortion>0 || v->knob[5]>0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void voice_init(voice_t *v) {
  memset(v, 0, sizeof(*v));
  v->p = (voice_params_t){
    .cutoff = 0.4f, .resonance = 0.6f, .envmod = 0.5f, .decay = 0.4f,
    .accent = 0.6f, .tune = 0.5f, .volume = 0.8f, .distortion = 0.0f, .wave = WAVE_SAW,
    .dist_drive = VOICE_PROFILE_DRIVE, .dist_type = 0, .delay_time = 0.75f, .delay_amount = 0.0f,
  };
  for (int i = 0; i < VOICE_MAX_NOTES; i++) v->osc[i].last_semis = -1000.0f;
  v->osc[0].pitch = v->osc[0].pitch_target = v->pitch_target = 45;   // A2
  v->osc[0].active = true;
  v->n_notes = 1;

  // constants
  v->c.glide_coef      = 1.0f - fast_exp(-T_BLOCK / 0.06f);
  v->c.vca_attack_step = T_BLOCK / 0.003f;
  v->c.vca_hold_mul    = fast_exp(-T_BLOCK / 3.0f);
  v->c.vca_release_mul = fast_exp(-T_BLOCK / 0.008f);
  v->g_hp = Q30(coef_onepole(100.0f, FS_FILTER));   // feedback high-pass at 100 Hz
  v->g_dc = Q30(coef_onepole(10.0f, VOICE_FS));
  v->g_dl_lp = Q30(coef_onepole(3500.0f, VOICE_FS));
  v->g_dl_hp = Q30(coef_onepole(150.0f, VOICE_FS));
  v->bpm = 120.0f;

  curves_init();
  v->profile.knob_coef=1.0f-fast_exp(logf(0.995f)*44100.0f*T_BLOCK);
  v->profile.acc_follow=1.0f-fast_exp(logf(0.99973f)*44100.0f*T_BLOCK);
  v->profile.acc_amp=1.0f-fast_exp(logf(0.9805f)*44100.0f*T_BLOCK);
  v->knob[0]=v->p.cutoff;v->knob[1]=v->p.resonance;
  v->knob[2]=v->p.envmod;v->knob[3]=sqrtf(v->p.decay);
  v->knob[4]=0.9801f*v->p.accent;v->knob[5]=v->p.distortion;
  derive_params(v);
  v->dist_makeup=v->dist_makeup_target;
  control_update(v);
  v->zG+=v->zG_step*VOICE_BLOCK;v->zkc+=v->zkc_step*VOICE_BLOCK;v->zr+=v->zr_step*VOICE_BLOCK;
  for(int i=0;i<4;i++)v->za[i]+=v->za_step[i]*VOICE_BLOCK;
  v->soft_gain=v->od_gain_n[1];
  v->sine_gain=v->drive/256;
  v->osc[0].phase_inc = v->osc[0].target_inc;
  v->osc[0].gain = Q28(1.0f);
  v->post_gain = ONE_Q28;
}

void voice_set_tempo(voice_t *v, float bpm) {
  if (bpm == v->bpm) return;
  v->bpm = bpm;
  delay_target(v);
}

static float normalized(float x) {
  if (!(x>=0)) return 0; // includes NaN
  return x>1 ? 1 : x;
}
void voice_set_params(voice_t *v, const voice_params_t *p) {
  v->p=*p;
  v->p.cutoff=normalized(p->cutoff);v->p.resonance=normalized(p->resonance);
  v->p.envmod=normalized(p->envmod);v->p.decay=normalized(p->decay);
  v->p.accent=normalized(p->accent);v->p.distortion=normalized(p->distortion);
  v->p.volume=normalized(p->volume);v->p.tune=normalized(p->tune);
  v->p.delay_time=normalized(p->delay_time);v->p.delay_amount=normalized(p->delay_amount);
  v->p.dist_drive=normalized(p->dist_drive);
  v->p.dist_type=p->dist_type ? 1 : 0;
  v->p.wave=p->wave==WAVE_SQUARE ? WAVE_SQUARE : WAVE_SAW;
  derive_params(v);
}

void voice_chord_on(voice_t *v, const uint8_t *notes, int n, bool accent, voice_mode_t mode) {
  if (n < 1) return;
  if (n > VOICE_MAX_NOTES) n = VOICE_MAX_NOTES;
  uint8_t sorted[VOICE_MAX_NOTES];
  for (int i = 0; i < n; i++) {                       // insertion sort, lowest first
    int j = i;
    while (j > 0 && sorted[j - 1] > notes[i]) { sorted[j] = sorted[j - 1]; j--; }
    sorted[j] = notes[i];
  }
  bool sounding = v->gate;
  bool glide = mode == VOICE_SLIDE && sounding;
  for (int i = 0; i < VOICE_MAX_NOTES; i++) {
    voice_osc_t *o = &v->osc[i];
    if (i < n) {
      bool was = o->active;
      o->pitch_target = (float)sorted[i];
      if (glide && was) {
        o->gliding = true;                            // slide this note to its new pitch
      } else {
        o->gliding = false;
        o->pitch = o->pitch_target;
        if (!was && sounding && mode != VOICE_RETRIGGER) {
          // a note joining a sounding chord: snap its frequency, fade it in
          o->last_semis = -1000.0f;
        }
      }
      o->active = true;
    } else {
      o->active = false;                              // fades out over one block
    }
  }
  v->n_notes = n;
  v->pitch_target = (float)sorted[0];
  if (mode == VOICE_RETRIGGER || !sounding) {
    v->accented = accent;
    v->gate = true;
    v->meg = 1.0f;
    v->vca_attack = true;
  } else if (mode == VOICE_SLIDE) {
    v->accented = accent;
  }
}

void voice_note_on(voice_t *v, int note, bool accent, bool legato) {
  uint8_t n = (uint8_t)note;
  voice_chord_on(v, &n, 1, accent, legato ? VOICE_SLIDE : VOICE_RETRIGGER);
}

void voice_note_off(voice_t *v) {
  v->gate = false;
}

static void render_block(voice_t *v, int16_t *out, int n) {
  // oscillators that are playing or still fading out
  voice_osc_t *live[VOICE_MAX_NOTES];
  int n_live = 0;
  for (int k = 0; k < VOICE_MAX_NOTES; k++) {
    voice_osc_t *o = &v->osc[k];
    if (o->active || o->gain != 0 || o->gain_step != 0) live[n_live++] = o;
  }
  bool square = v->p.wave == WAVE_SQUARE;

  for (int i = 0; i < n; i++) {
    v->dist_wet += v->dist_wet_step;
    v->dist_mode += v->dist_mode_step;
    v->dist_makeup += v->dist_makeup_step;
    v->soft_gain += v->soft_gain_step;
    v->sine_gain += v->sine_gain_step;
    v->out_gain += v->out_gain_step;

    int32_t x = 0;
    for (int k = 0; k < n_live; k++) {
      voice_osc_t *o = live[k];
      o->phase_inc += (uint32_t)o->phase_inc_step;
      o->gain += o->gain_step;
      x += mul_q28(oscillator(o, square), o->gain);
    }
    x >>= 1;                                     // oscillators at half scale into the filter

    v->zG+=v->zG_step;v->zkc+=v->zkc_step;v->zr+=v->zr_step;
    v->za[0]+=v->za_step[0];v->za[1]+=v->za_step[1];v->za[2]+=v->za_step[2];v->za[3]+=v->za_step[3];
    {
      // 1.0.0 zero-delay-feedback ladder: four trapezoidal one-pole stages,
      // with the feedback loop solved within the sample (no unit delay), so
      // the resonance stays in tune up to 18 kHz without oversampling.
      // Normalised domain (signal / 0.624), as the stages of the old core.
      const int32_t INV_VT=Q28(1.0f/0.624f), LIM=Q28(3.5f);
      int32_t X=mul_q28(x,INV_VT);
      // Linear part of the ladder: y4 = G^4 U + sum(a_i s_i). Solve the
      // feedback U = X - kc (y4 - L) for this very sample.
      int32_t L=mul_q28(v->fb_lp,INV_VT);
      int32_t sig=mul_q30(v->za[0],v->zs[0])+mul_q30(v->za[1],v->zs[1])
                 +mul_q30(v->za[2],v->zs[2])+mul_q30(v->za[3],v->zs[3]);
      int32_t d=sig-L;
      if(d>LIM)d=LIM;
      if(d<-LIM)d=-LIM;
      int32_t U=mul_q30(X-mul_q28(v->zkc,d),v->zr);
      if(U>LIM)U=LIM;
      if(U<-LIM)U=-LIM;
      int32_t in=rational_q28(U);                // input saturation (knee at 3)
      int32_t y=0;
      for (int k = 0; k < 4; k++) {
        int32_t vv=mul_q30(v->zG,in-v->zs[k]);
        y=vv+v->zs[k];
        v->zs[k]=y+vv;
        in=k<3?rational_q28(y):y;                 // gentle saturation between stages
      }
      v->y4=mul_q28(y,TWO_VT);
      v->fb_lp+=mul_q30(v->y4-v->fb_lp,v->g_hp);  // feedback high-pass (100 Hz)
    }

    int32_t y = mul_q28(v->y4, v->out_gain);
    // Reserve headroom for chord makeup and delay feedback at full level.
    if (y>3*ONE_Q28) y=3*ONE_Q28;
    if (y<-3*ONE_Q28) y=-3*ONE_Q28;
    if (v->dist_on) y = distortion(v, y);
    else v->dist_prev = y;
    v->post_gain += v->post_gain_step;
    if (v->post_gain != ONE_Q28) y = mul_q28(y, v->post_gain);
    if (v->dl_on) {
      y = delay(v, y);
      if (v->dl_off && --v->dl_off == 0) v->dl_on = false;
    }

    y = limit(y);
    int32_t s = y >> 13;                        // Q28 -> Q15
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    out[i] = (int16_t)s;
  }
}

void voice_render(voice_t *v, int16_t *out, int n) {
  while (n > 0) {
    if (!v->control_remaining) {
      control_update(v);
      v->control_remaining = VOICE_BLOCK;
    }
    int chunk = n < v->control_remaining ? n : v->control_remaining;
    render_block(v, out, chunk);
    v->control_remaining -= (uint8_t)chunk;
    out += chunk;
    n -= chunk;
  }
}
