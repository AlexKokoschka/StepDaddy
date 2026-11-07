#include <math.h>
#include <stddef.h>
#include "audio.h"
#include "telemetry.h"
#include "seq303.h"
#define AUDIO_SAMPLE_RATE 48000
#include "pico/stdlib.h"




// ---------------------------------------------------------------------------
// Ring buffer: core 1 writes, core 0 reads (mono samples)
// ---------------------------------------------------------------------------

#define RING_SIZE     1024                  // power of two
#define RING_MASK     (RING_SIZE - 1)
#define RING_TARGET   256                   // frames kept ready (~2.7 ms): the delay from playing to hearing

static int16_t ring[RING_SIZE];
static volatile uint32_t ring_wr;           // written only by core 1
static volatile uint32_t ring_rd;           // written only by core 0
static volatile uint32_t underruns;

uint32_t audio_underruns(void) { return underruns; }

void audio_reset_stream(void) {
  uint32_t wr = ring_wr;
  if (wr - ring_rd > 2 * (AUDIO_SAMPLE_RATE / 1000)) ring_rd = wr - 2 * (AUDIO_SAMPLE_RATE / 1000);
}

void audio_read_stereo(int16_t *dst, int n) {
  uint32_t rd = ring_rd;
  uint32_t avail = ring_wr - rd;
  __dmb();
  for (int i = 0; i < n; i++) {
    int16_t s = 0;
    if (avail) { s = ring[rd & RING_MASK]; rd++; avail--; }
    else underruns++;
    dst[2 * i] = s;
    dst[2 * i + 1] = s;
  }
  __dmb();
  ring_rd = rd;
}

// ---------------------------------------------------------------------------
// Core 0 -> core 1: event queue and parameter snapshot
// ---------------------------------------------------------------------------

enum { EV_NOTE_ON, EV_NOTE_OFF, EV_SEQ_START, EV_SEQ_STOP, EV_SEQ_START_EXT, EV_EXT_STEP, EV_CHORD_ON };
typedef struct { uint8_t type, note; bool accent, legato; uint32_t value; uint8_t notes[VOICE_MAX_NOTES], n, mode; } event_t;

#define EVQ_SIZE 32
static event_t evq[EVQ_SIZE];
static volatile uint32_t ev_dropped,mq_dropped;
uint32_t audio_event_dropped(void){return ev_dropped;}
uint32_t audio_midi_dropped(void){return mq_dropped;}
static volatile uint32_t ev_wr, ev_rd;

static void post(event_t e) {
  uint32_t wr = ev_wr;
  if (wr - ev_rd >= EVQ_SIZE) {ev_dropped++;return;}      // queue full: drop (shouldn't happen)
  evq[wr % EVQ_SIZE] = e;
  __dmb();
  ev_wr = wr + 1;
}

void audio_note_on(int note, bool accent, bool legato) {
  post((event_t){.type = EV_NOTE_ON, .note = (uint8_t)note, .accent = accent, .legato = legato});
}
void audio_chord_on(const uint8_t *notes, int n, bool accent, voice_mode_t mode) {
  event_t e = {.type = EV_CHORD_ON, .accent = accent, .mode = (uint8_t)mode};
  if (n > VOICE_MAX_NOTES) n = VOICE_MAX_NOTES;
  for (int i = 0; i < n; i++) e.notes[i] = notes[i];
  e.n = (uint8_t)n;
  post(e);
}
void audio_note_off(void) { post((event_t){.type = EV_NOTE_OFF}); }
void audio_seq(bool run) { post((event_t){.type = run ? EV_SEQ_START : EV_SEQ_STOP}); }
void audio_seq_ext_start(void) { post((event_t){.type = EV_SEQ_START_EXT}); }
void audio_ext_step(uint32_t count) { post((event_t){.type = EV_EXT_STEP, .value = count}); }

// Sequencer notes out: core 1 -> core 0 queue (sent as USB MIDI by core 0)
#define MIDIQ_SIZE 64
static struct { uint8_t note, vel; } midiq[MIDIQ_SIZE];
static volatile uint32_t mq_wr, mq_rd;

static void midi_out_push(uint8_t note, uint8_t vel) {      // core 1
  uint32_t wr = mq_wr;
  if (wr - mq_rd >= MIDIQ_SIZE) {mq_dropped++;return;}
  midiq[wr % MIDIQ_SIZE].note = note;
  midiq[wr % MIDIQ_SIZE].vel = vel;
  __dmb();
  mq_wr = wr + 1;
}

bool audio_midi_out_pop(uint8_t *note, uint8_t *vel) {       // core 0
  uint32_t rd = mq_rd;
  if (rd == mq_wr) return false;
  __dmb();
  *note = midiq[rd % MIDIQ_SIZE].note;
  *vel = midiq[rd % MIDIQ_SIZE].vel;
  __dmb();
  mq_rd = rd + 1;
  return true;
}

// Seqlock: core 0 bumps the counter to odd, writes, bumps to even.
static voice_params_t shared_params;
static volatile uint32_t params_seq;

void audio_set_params(const voice_params_t *p) {
  params_seq++;
  __dmb();
  shared_params = *p;
  __dmb();
  params_seq++;
}

// ---------------------------------------------------------------------------
// Core 1
// ---------------------------------------------------------------------------

static voice_t voice;
static volatile uint32_t profile_request, profile_seq;
static volatile audio_profile_t profile_published;
void audio_profile_reset(void) { profile_request++; __dmb(); }
bool audio_profile_reset_pending(void) {
 return profile_request != profile_published.generation;
}
bool audio_profile_get(audio_profile_t *out) {
 // Bounded seqlock read: never stall USB waiting for the audio core.
 for (int attempt=0;attempt<3;attempt++) {
  uint32_t seq=profile_seq; if(seq&1)continue;
  __dmb(); *out=profile_published; __dmb();
  if(seq==profile_seq)return true;
 }
 return false;
}
static void profile_publish(const audio_profile_t *value) {
 profile_seq++; __dmb(); profile_published=*value; __dmb(); profile_seq++;
}

static volatile uint8_t cpu_avg, cpu_peak;
static volatile uint16_t cpu_peak_max;
uint16_t audio_cpu_peak_max(void){return cpu_peak_max;}

uint8_t audio_cpu_avg(void)  { return cpu_avg; }
uint8_t audio_cpu_peak(void) { return cpu_peak; }

static void handle_events(void) {
  while (ev_rd != ev_wr) {
    event_t e = evq[ev_rd % EVQ_SIZE];
    __dmb();
    ev_rd++;
    switch (e.type) {
      // Live notes (pads, MIDI) own the voice while held; the sequencer waits.
      case EV_NOTE_ON:  seq_voice_muted = true;  voice_note_on(&voice, e.note, e.accent, e.legato); break;
      case EV_CHORD_ON: seq_voice_muted = true;  voice_chord_on(&voice, e.notes, e.n, e.accent, (voice_mode_t)e.mode); break;
      case EV_NOTE_OFF: seq_voice_muted = false; voice_note_off(&voice); break;
      case EV_SEQ_START: seq_start(&voice); break;
      case EV_SEQ_STOP:  seq_stop(&voice); break;
      case EV_SEQ_START_EXT: seq_start_ext(&voice); break;
      case EV_EXT_STEP:  seq_ext_step(&voice, e.value); break;
    }
  }
}

static void apply_params(void) {
  static uint32_t applied_seq;
  uint32_t s1 = params_seq;
  if ((s1 & 1) || s1 == applied_seq) return;
  __dmb();
  voice_params_t p = shared_params;
  __dmb();
  if (params_seq != s1) return;            // changed while copying: try next block
  applied_seq = s1;
  voice_set_params(&voice, &p);
}

// ---------------------------------------------------------------------------
// Metronome: a short click on every beat (higher on beat 1 of each 4/4 bar),
// mixed into the output. Two clicks are made once at start-up.
// ---------------------------------------------------------------------------

#define CLICK_LEN 720                               // 15 ms
static int16_t click_hi[CLICK_LEN], click_lo[CLICK_LEN];
static volatile bool metro_on;
static const int16_t *click;                       // playing click, or NULL
static int click_pos;
static uint32_t metro_seen_step;

void audio_metronome(bool on) { metro_on = on; }

// 0.3.2: Yummy audio mute (Shift+Mute pad 3 on the StepBro level). The synth
// keeps running underneath, so unmuting comes back in time; only its output
// fades, over 240 samples (5 ms), so there is no click. Delay echoes are part
// of that output and go silent with it. The metronome is not muted.
static volatile bool mute_on;
static int32_t mute_gain = 32768;                  // Q15, owned by the audio side
void audio_mute(bool on) { mute_on = on; }
__attribute__((noinline, optimize("Os"))) static void apply_mute(int16_t *block, int n) {
  int32_t target = mute_on ? 0 : 32768;
  if (mute_gain == target && target == 32768) return;
  for (int i = 0; i < n; i++) {
    if (mute_gain < target) { mute_gain += 137; if (mute_gain > target) mute_gain = target; }
    else if (mute_gain > target) { mute_gain -= 137; if (mute_gain < target) mute_gain = target; }
    block[i] = (int16_t)((block[i] * mute_gain) >> 15);
  }
}

static void make_clicks(void) {
  for (int i = 0; i < CLICK_LEN; i++) {
    float t = (float)i / AUDIO_SAMPLE_RATE;
    float env = expf(-t * 300.0f);
    click_hi[i] = (int16_t)(9000.0f * env * sinf(6.2831853f * 2000.0f * t));
    click_lo[i] = (int16_t)(7000.0f * env * sinf(6.2831853f * 1000.0f * t));
  }
}

static void metronome(int16_t *block, int n) {
  uint32_t c = seq_step_count;
  if (c != metro_seen_step) {                       // a step started in this block
    metro_seen_step = c;
    if (metro_on && seq_playhead >= 0 && c > 0 && (c - 1) % 4 == 0) {
      click = ((c - 1) % 16 == 0) ? click_hi : click_lo;
      click_pos = 0;
    }
  }
  if (!click) return;
  for (int i = 0; i < n && click_pos < CLICK_LEN; i++, click_pos++) {
    int32_t s = block[i] + click[click_pos];
    block[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
  }
  if (click_pos >= CLICK_LEN) click = NULL;
}

static profile_state_t profile;
static uint32_t busy_us, window_start, peak_us;
static bool audio_ready;
void audio_service(void) {
  if(!audio_ready)return;
  int16_t block[VOICE_BLOCK];
  const uint32_t block_us=1000000u*VOICE_BLOCK/AUDIO_SAMPLE_RATE;
  uint32_t request=profile_request;__dmb();
  if(request!=profile.value.generation){profile_reset(&profile,request,time_us_64());profile_publish(&profile.value);}
  // Bounded work: USB IRQ can consume samples while the main loop fills ahead.
  for(unsigned blocks=0;blocks<RING_TARGET/VOICE_BLOCK;blocks++) {
    if(ring_wr-ring_rd>=RING_TARGET)break;
    uint32_t t0=time_us_32();handle_events();uint32_t t1=time_us_32();
    apply_params();voice_set_tempo(&voice,seq_bpm);uint32_t t2=time_us_32();
    seq_tick(&voice,VOICE_BLOCK);uint32_t t3=time_us_32();
    voice_render(&voice,block,VOICE_BLOCK);uint32_t t4=time_us_32();
    apply_mute(block,VOICE_BLOCK);metronome(block,VOICE_BLOCK);
    uint32_t wr=ring_wr;for(int i=0;i<VOICE_BLOCK;i++)ring[(wr+i)&RING_MASK]=block[i];
    __dmb();ring_wr=wr+VOICE_BLOCK;
    uint32_t dt=time_us_32()-t0;
    const uint32_t stages[PROFILE_STAGES]={t1-t0,t2-t1,t3-t2,t4-t3,(t0+dt)-t4};
    profile_record(&profile,stages,time_us_64());busy_us+=dt;if(dt>peak_us)peak_us=dt;
  }
  uint32_t now=time_us_32();
  if(now-window_start>=1000000u){
    uint32_t avg=busy_us*100u/(now-window_start),peak=peak_us*100u/block_us;
    if(peak>cpu_peak_max)cpu_peak_max=(uint16_t)(peak>65535?65535:peak);
    cpu_avg=(uint8_t)(avg>127?127:avg);cpu_peak=(uint8_t)(peak>127?127:peak);
    profile_publish(&profile.value);busy_us=peak_us=0;window_start=now;
  }
}

void audio_start_core1(void) {
  voice_init(&voice);
  seq_init();
  make_clicks();
  seq_midi_hook = midi_out_push;
  seq_ext_lead = RING_TARGET;          // synced steps start early by the buffer's delay
  profile_init(&profile,time_us_64());profile_publish(&profile.value);
  window_start=time_us_32();audio_ready=true;audio_service();
}
