#include "seq303.h"
#include <string.h>

seq_pattern_t    seq_patterns[SEQ_PATTERNS];
volatile float   seq_bpm = 125.0f;
volatile uint8_t seq_pattern_next;
volatile uint8_t seq_pattern_cur;
volatile uint32_t seq_step_count;
uint8_t           seq_chain[SEQ_CHAIN_MAX];
volatile uint8_t  seq_chain_len;
volatile uint32_t seq_chain_version;

// The chain being played (core 1's copy)
static uint8_t  chain[SEQ_CHAIN_MAX];
static int      chain_len, chain_pos;
static uint32_t chain_version = 0xFFFFFFFFu;

static int pattern_len(int p) { return seq_patterns[p].length ? seq_patterns[p].length : SEQ_STEPS; }

// Takes a new chain (or the end of one) if core 0 changed it; true if so.
static bool chain_reload(void) {
  uint32_t ver = seq_chain_version;
  if (ver == chain_version) return false;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  int n = seq_chain_len;
  if (n > SEQ_CHAIN_MAX) n = SEQ_CHAIN_MAX;
  for (int i = 0; i < n; i++) chain[i] = seq_chain[i] % SEQ_PATTERNS;
  chain_len = n >= 2 ? n : 0;
  chain_pos = 0;
  chain_version = ver;
  return true;
}

// Pattern to play from the start (playback starts, or a new chain).
static void pattern_first(void) {
  chain_reload();
  chain_pos = 0;
  seq_pattern_cur = chain_len ? chain[0] : seq_pattern_next;
}

// At the end of a pattern: the next one in the chain, a new chain, or the
// queued single pattern.
static void pattern_next(void) {
  if (!chain_reload() && chain_len) chain_pos = (chain_pos + 1) % chain_len;
  seq_pattern_cur = chain_len ? chain[chain_pos] : seq_pattern_next;
}
volatile int8_t  seq_playhead = -1;
volatile uint16_t seq_step_phase;
volatile int8_t  seq_key_shift;
bool             seq_voice_muted;

int seq_shift_for_root(int root) {
  return ((root - SEQ_REF_ROOT) % 12 + 12 + 6) % 12 - 6;      // -6 .. +5
}

static bool     playing;
static int      step;
static uint32_t pos_fx;          // position within the step, samples x 65536
static bool     need_step_start;
static bool     hold, gate_closed, prev_slide;

// Demo pattern in slot 1 (the same one as the computer renders), so there is
// something to hear straight away.            pitch  on  acc slide tie
static const seq_step_t demo[16] = {
  {45, 1, 1, 0, 0, {0}}, {45, 1, 0, 0, 0, {0}}, {57, 1, 0, 1, 0, {0}}, {55, 1, 0, 0, 0, {0}},
  { 0, 0, 0, 0, 0, {0}}, {45, 1, 1, 0, 0, {0}}, {48, 1, 0, 1, 0, {0}}, {51, 1, 0, 0, 0, {0}},
  {45, 1, 0, 0, 0, {0}}, { 0, 1, 0, 0, 1, {0}}, {57, 1, 1, 1, 0, {0}}, {45, 1, 0, 0, 0, {0}},
  {43, 1, 0, 0, 0, {0}}, { 0, 0, 0, 0, 0, {0}}, {48, 1, 1, 0, 0, {0}}, {50, 1, 0, 1, 0, {0}},
};

void seq_init(void) {
  memset(seq_patterns, 0, sizeof(seq_patterns));
  for (int p = 0; p < SEQ_PATTERNS; p++) seq_patterns[p].length = SEQ_STEPS;
  memcpy(seq_patterns[0].steps, demo, sizeof(demo));
}

// ---------------------------------------------------------------------------
// Scales: 12-bit masks of the notes above the root
// ---------------------------------------------------------------------------

static const uint16_t SCALE_MASK[SCALE_COUNT] = {
  0xFFF,                                                           // chromatic
  (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // natural minor
  (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   // major
  (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         // minor pentatonic
  (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // phrygian
  (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              // blues
};

bool seq_in_scale(int pitch, seq_scale_t scale, int root) {
  int degree = ((pitch - root) % 12 + 12) % 12;
  return (SCALE_MASK[scale] >> degree) & 1;
}

int seq_scale_move(int pitch, int degrees, seq_scale_t scale, int root) {
  int dir = degrees > 0 ? 1 : -1;
  for (int n = degrees > 0 ? degrees : -degrees; n > 0; n--) {
    int p = pitch;
    do { p += dir; } while (!seq_in_scale(p, scale, root) && p > 0 && p < 127);
    if (p < SEQ_PITCH_MIN || p > SEQ_PITCH_MAX) break;      // stay inside the range
    pitch = p;
  }
  return pitch;
}

int seq_scale_snap(int pitch, seq_scale_t scale, int root) {
  if (seq_in_scale(pitch, scale, root)) return pitch;
  for (int d = 1; d < 12; d++) {
    if (seq_in_scale(pitch + d, scale, root)) return pitch + d;   // up wins a tie
    if (seq_in_scale(pitch - d, scale, root)) return pitch - d;
  }
  return pitch;
}

bool seq_playing(void) { return playing; }

int seq_record_step(int playhead, uint16_t phase, int length, float bpm, float latency_ms) {
  if (length <= 0) length = SEQ_STEPS;
  float step_ms = 60000.0f / bpm / 4.0f;
  float pos = (float)playhead + phase / 65536.0f - latency_ms / step_ms;
  int idx = (int)(pos + 0.5f + (float)length) % length;   // +length keeps it positive
  return idx;
}

// ---------------------------------------------------------------------------
// MIDI note output (sequencer notes, for recording into a DAW)
// ---------------------------------------------------------------------------

void (*seq_midi_hook)(uint8_t note, uint8_t velocity);   // velocity 0 = note off
volatile bool seq_midi_enabled;
volatile bool seq_mute_request;          // board mute while synced (core 0)
volatile bool seq_muted;                 // applied at once (core 1)
static uint8_t out_notes[SEQ_MAX_NOTES];
static int     out_n;

static bool in_set(const uint8_t *set, int n, uint8_t x) {
  for (int i = 0; i < n; i++) if (set[i] == x) return true;
  return false;
}

static void out_off(void) {
  if (seq_midi_hook) for (int i = 0; i < out_n; i++) seq_midi_hook(out_notes[i], 0);
  out_n = 0;
}

static void out_on(const uint8_t *notes, int n, bool accent, bool legato) {
  if (!seq_midi_enabled || !seq_midi_hook) { out_off(); return; }
  if (!legato) out_off();
  uint8_t vel = accent ? 127 : 100;
  for (int i = 0; i < n; i++) if (!in_set(out_notes, out_n, notes[i])) seq_midi_hook(notes[i], vel);
  // slide: new notes on before the old ones end (overlap = slide in a DAW)
  for (int i = 0; i < out_n; i++) if (!in_set(notes, n, out_notes[i])) seq_midi_hook(out_notes[i], 0);
  for (int i = 0; i < n; i++) out_notes[i] = notes[i];
  out_n = n;
}

// --- MIDI ahead of the sound (synced) ---------------------------------------
volatile uint32_t seq_midi_lead;

// What a step does to the MIDI output.
typedef enum { MA_NONE, MA_OFF, MA_ON } midi_kind_t;
typedef struct {
  uint8_t kind, n, accent, legato;
  uint8_t notes[SEQ_MAX_NOTES];
} midi_act_t;

static midi_act_t ahead_act;             // what was sent ahead for ahead_count
static uint32_t   ahead_count;
static bool       ahead_valid;
static bool       early_off_done;        // the current step's early note-off was sent

static bool act_equal(const midi_act_t *a, const midi_act_t *b) {
  if (a->kind != b->kind) return false;
  if (a->kind != MA_ON) return true;
  return a->n == b->n && a->accent == b->accent && a->legato == b->legato &&
         memcmp(a->notes, b->notes, a->n) == 0;
}

static void act_apply(const midi_act_t *a) {
  if (a->kind == MA_ON) out_on(a->notes, a->n, a->accent, a->legato);
  else if (a->kind == MA_OFF) out_off();
}

int seq_step_get(const seq_pattern_t *pat, const seq_step_t *s, uint8_t *notes) {
  int shift = seq_key_shift + pat->transpose;
  int n = 0;
  notes[n++] = (uint8_t)(s->pitch + shift);
  for (int i = 0; i < 3; i++) if (s->chord[i]) notes[n++] = (uint8_t)(s->chord[i] + shift);
  return n;
}

int seq_step_notes(const seq_pattern_t *pat, const seq_step_t *s, uint8_t *notes) {
  int n = seq_step_get(pat, s, notes);
  return pat->poly ? n : 1;
}

void seq_step_set(const seq_pattern_t *pat, seq_step_t *s, const uint8_t *notes, int n) {
  if (n < 1) return;
  if (n > SEQ_MAX_NOTES) n = SEQ_MAX_NOTES;
  uint8_t sorted[SEQ_MAX_NOTES];
  for (int i = 0; i < n; i++) {                       // lowest first
    int j = i;
    while (j > 0 && sorted[j - 1] > notes[i]) { sorted[j] = sorted[j - 1]; j--; }
    sorted[j] = notes[i];
  }
  int shift = seq_key_shift + pat->transpose;
  s->pitch = (uint8_t)(sorted[0] - shift);
  for (int i = 0; i < 3; i++) s->chord[i] = (uint8_t)(i + 1 < n ? sorted[i + 1] - shift : 0);
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

static bool    ext_mode;                 // steps come from an external clock
static uint32_t ext_origin;              // song position (16ths) where the current pattern began
static bool    ext_have_origin;

// Synced timing. Each step is started seq_ext_lead samples *before* its clock
// tick is expected, so that after the audio buffer's delay it's heard on the
// tick. The next tick's time is predicted from the last one plus one 16th at
// the measured tempo; every tick that arrives re-anchors the prediction, so
// errors never add up. Only one step is ever predicted ahead.
uint32_t       seq_ext_lead;             // samples (set by the firmware: its buffer size)
static uint32_t now_samples;             // sample clock of the sequencer
static bool    ext_locked;               // following consecutive ticks
static uint32_t ext_last_count, ext_last_at;   // last tick received and when
static bool    ext_played_valid;
static uint32_t ext_played;              // count of the last step started
static bool    ext_due_valid;
static uint32_t ext_due_count, ext_due_at;     // next step to start and when

static float sixteenth_samples(void);
static float sixteenth_samples_fwd(void) { return sixteenth_samples(); }

// MIDI is sent ahead only while synced, with a lead set and MIDI out on.
static bool midi_ahead(void) { return ext_mode && seq_midi_lead && seq_midi_enabled && seq_midi_hook; }

// The lead in samples, kept under 3/4 of a 16th so a step's notes never go
// out before the previous step has started.
static uint32_t midi_lead_samples(void) {
  float max = sixteenth_samples_fwd() * 0.75f;
  return seq_midi_lead < max ? seq_midi_lead : (uint32_t)max;
}

// What a step sends: notes (legato = slide into it), a note-off, or nothing.
static void step_action(const seq_pattern_t *pat, int stp, bool legato, midi_act_t *a) {
  const seq_step_t *s = &pat->steps[stp];
  memset(a, 0, sizeof *a);
  if (seq_muted)                { a->kind = MA_OFF; return; }
  if (seq_voice_muted)          { a->kind = MA_NONE; return; }   // a live note owns the output
  if (s->on && !s->tie) {
    a->kind = MA_ON;
    a->n = (uint8_t)seq_step_notes(pat, s, a->notes);
    a->accent = s->accent;
    a->legato = legato;
  } else if (s->on && s->tie)   { a->kind = MA_NONE; }
  else                          { a->kind = MA_OFF; }
}

void seq_start(voice_t *v) {
  (void)v;
  playing = true;
  ext_mode = false;
  step = 0;
  pos_fx = 0;
  need_step_start = true;
  prev_slide = false;
  seq_muted = false;
  pattern_first();
  seq_playhead = 0;
  seq_step_count = 0;
}

void seq_start_ext(voice_t *v) {
  (void)v;
  playing = true;
  ext_mode = true;
  need_step_start = false;
  ext_have_origin = false;
  ext_locked = false;
  ext_played_valid = false;
  ext_due_valid = false;
  ahead_valid = false;
  early_off_done = false;
  prev_slide = false;
  seq_muted = seq_mute_request;
  pattern_first();
  seq_playhead = -1;                     // nothing until the first clock step arrives
}

static float step_samples(int s) {       // nominal length of step s, with swing
  float bpm = seq_bpm;
  if (bpm < 20.0f) bpm = 20.0f;
  float pair = (float)VOICE_FS * 60.0f / bpm / 2.0f;
  float share = 0.5f + 0.25f * seq_patterns[seq_pattern_cur].swing / 100.0f;
  return pair * ((s & 1) ? 1.0f - share : share);
}

static float sixteenth_samples(void) {
  float bpm = seq_bpm < 20.0f ? 20.0f : seq_bpm;
  return (float)VOICE_FS * 60.0f / bpm / 4.0f;
}

// Swing in sync follows the song's 16ths: odd ones start late.
static uint32_t ext_swing(uint32_t count) {
  return (count & 1) ? (uint32_t)(step_samples(0) - sixteenth_samples()) : 0;
}

// Schedules step `count` at `at` (in the past = now).
static void ext_schedule(uint32_t count, uint32_t at) {
  ext_due_valid = true;
  ext_due_count = count;
  ext_due_at = at;
}

void seq_ext_step(voice_t *v, uint32_t count) {
  (void)v;
  if (!playing || !ext_mode) return;
  bool next_in_line = ext_locked && count == ext_last_count + 1;
  // Smooth the tick times: USB delivers MIDI in 1 ms packets, so single ticks
  // jitter. Follow a quarter of each tick's deviation from the prediction
  // (big jumps are taken in full).
  uint32_t at_tick = now_samples;
  if (next_in_line) {
    uint32_t predicted = ext_last_at + (uint32_t)sixteenth_samples();
    int32_t dev = (int32_t)(now_samples - predicted);
    if (dev > -480 && dev < 480) at_tick = predicted + (uint32_t)(dev / 4);
  }
  ext_locked = true;
  ext_last_count = count;
  ext_last_at = at_tick;
  if (ext_played_valid && ext_played == count) {   // already started early: all good
    if (ext_due_valid && ext_due_count == count) ext_due_valid = false;
    return;
  }
  // Not started yet: the prediction was late (tempo went up) or this is the
  // first tick / a jump. Start it as near its intended time as still possible.
  uint32_t at = now_samples + ext_swing(count);
  if (next_in_line) at -= seq_ext_lead;
  ext_schedule(count, at);
}

// Step index for a song position; handles pattern changes at the boundary.
static void ext_enter_count(uint32_t count) {
  if (!ext_have_origin || count < ext_origin || count - ext_origin > 1024) {
    // Joining: line up with the song (bar 1 = step 1 of the pattern, or of
    // the chain's first pattern).
    if (chain_len) {
      uint32_t total = 0;
      for (int i = 0; i < chain_len; i++) total += (uint32_t)pattern_len(chain[i]);
      uint32_t t = count % total;
      int i = 0;
      while (t >= (uint32_t)pattern_len(chain[i])) { t -= (uint32_t)pattern_len(chain[i]); i++; }
      chain_pos = i;
      seq_pattern_cur = chain[i];
      ext_origin = count - t;
    } else {
      int len = pattern_len(seq_pattern_cur);
      ext_origin = count - count % (uint32_t)len;
    }
    ext_have_origin = true;
  }
  int len = pattern_len(seq_pattern_cur);
  // The pattern ends after `len` steps: on to the next one (new patterns
  // start from their step 1, whatever their length).
  while (count - ext_origin >= (uint32_t)len) {
    ext_origin += (uint32_t)len;
    pattern_next();
    len = pattern_len(seq_pattern_cur);
  }
  step = (int)(count - ext_origin);
}

// Pattern and step that song position `count` will play, without changing
// anything (for sending its MIDI ahead). False when it can't be known yet:
// joining, or a new chain about to be taken.
static bool ext_peek(uint32_t count, int *pat_out, int *step_out) {
  if (!ext_have_origin || count < ext_origin || count - ext_origin > 1024) return false;
  uint32_t origin = ext_origin;
  int cur = seq_pattern_cur, pos = chain_pos;
  int len = pattern_len(cur);
  while (count - origin >= (uint32_t)len) {
    if (seq_chain_version != chain_version) return false;
    origin += (uint32_t)len;
    if (chain_len) { pos = (pos + 1) % chain_len; cur = chain[pos]; }
    else cur = seq_pattern_next;
    len = pattern_len(cur);
  }
  *pat_out = cur;
  *step_out = (int)(count - origin);
  return true;
}

void seq_stop(voice_t *v) {
  playing = false;
  ext_mode = false;
  seq_playhead = -1;
  if (!seq_voice_muted) voice_note_off(v);
  out_off();
  ahead_valid = false;
}

static void start_step(voice_t *v) {
  const seq_pattern_t *pat = &seq_patterns[seq_pattern_cur];
  int len = pat->length ? pat->length : SEQ_STEPS;
  const seq_step_t *s = &pat->steps[step];
  const seq_step_t *next = &pat->steps[(step + 1) % len];
  bool sounding;
  uint8_t notes[SEQ_MAX_NOTES];
  int n = seq_step_notes(pat, s, notes);

  if (seq_muted) {
    sounding = false;
  } else if (seq_voice_muted) {              // a live note owns the voice: keep time only
    sounding = s->on && !s->tie;
  } else if (s->on && !s->tie) {
    bool legato = prev_slide && v->gate;
    voice_chord_on(v, notes, n, s->accent, legato ? VOICE_SLIDE : VOICE_RETRIGGER);
    sounding = true;
  } else if (s->on && s->tie) {
    sounding = v->gate;                      // a tie after a rest is just a rest
  } else {
    voice_note_off(v);
    sounding = false;
  }
  // MIDI: unless exactly this was already sent ahead for this step.
  midi_act_t act;
  step_action(pat, step, prev_slide && v->gate, &act);
  bool sent = ext_mode && ahead_valid && ext_played_valid && ahead_count == ext_played &&
              act_equal(&act, &ahead_act);
  if (!sent) act_apply(&act);
  ahead_valid = false;
  early_off_done = false;
  // 303 gate: open for half the step, unless this step slides or the next one is a tie
  hold = sounding && (s->slide || (next->on && next->tie));
  prev_slide = sounding && s->slide;
  gate_closed = !sounding;
  seq_playhead = (int8_t)step;
  seq_step_phase = 0;
  seq_step_count++;
  if (ext_mode) pos_fx = 0;                  // internal clock keeps the sub-block remainder
}

static void tick(voice_t *v, int n);

void seq_tick(voice_t *v, int n) {
  if (playing) tick(v, n);
  now_samples += (uint32_t)n;            // sample clock: start of the next block
}

static void tick(voice_t *v, int n) {

  uint32_t step_fx = (uint32_t)(step_samples(step) * 65536.0f);

  // Mute / unmute at once. Muting cuts the note now; unmuting plays the
  // current step too if it's still in its first quarter (a press just after
  // the beat still catches that note), otherwise the next step.
  if (seq_muted != seq_mute_request) {
    seq_muted = seq_mute_request;
    if (seq_muted) {
      if (!seq_voice_muted) voice_note_off(v);
      out_off();
      ahead_valid = false;
      gate_closed = true;
      hold = false;
      prev_slide = false;
    } else if (seq_playhead >= 0 && pos_fx < step_fx / 4 && !need_step_start &&
               !(ext_mode && ext_due_valid && ext_due_count != ext_played)) {
      uint32_t keep = pos_fx;
      start_step(v);
      pos_fx = keep;
    }
  }

  if (ext_mode) {
    // predict the next step once the current one has started
    if (!ext_due_valid && ext_locked && ext_played_valid && ext_played == ext_last_count) {
      uint32_t c = ext_last_count + 1;
      ext_schedule(c, ext_last_at + (uint32_t)sixteenth_samples() + ext_swing(c) - seq_ext_lead);
    }
    if (ext_due_valid && (int32_t)(now_samples + (uint32_t)n - ext_due_at) > 0) {
      ext_enter_count(ext_due_count);
      ext_played = ext_due_count;
      ext_played_valid = true;
      ext_due_valid = false;
      start_step(v);
    }
    // MIDI for the next step, ahead of its sound (the current step has started)
    if (midi_ahead() && ext_due_valid && ext_played_valid && ext_due_count == ext_played + 1 &&
        !(ahead_valid && ahead_count == ext_due_count) &&
        (int32_t)(now_samples + (uint32_t)n - (ext_due_at - midi_lead_samples())) > 0) {
      int p, st;
      if (ext_peek(ext_due_count, &p, &st)) {
        if (!early_off_done && !hold) { out_off(); early_off_done = true; }   // this step's end first
        step_action(&seq_patterns[p], st, prev_slide, &ahead_act);
        act_apply(&ahead_act);
        ahead_valid = true;
        ahead_count = ext_due_count;
        early_off_done = true;
      }
    }
  } else if (need_step_start) {
    start_step(v);
    need_step_start = false;
  }

  pos_fx += (uint32_t)n << 16;
  seq_step_phase = (uint16_t)((float)(pos_fx < step_fx ? pos_fx : step_fx) / (float)step_fx * 65535.0f);
  if (!gate_closed && !hold && pos_fx >= step_fx / 2) {
    if (!seq_voice_muted) voice_note_off(v);
    if (!midi_ahead()) out_off();
    gate_closed = true;
  }
  // Ahead of the sound, the note-off too (by the same lead, at most half a step).
  if (midi_ahead() && !early_off_done && !hold) {
    uint32_t lead_fx = midi_lead_samples() << 16;
    if (lead_fx > step_fx / 2) lead_fx = step_fx / 2;
    if (pos_fx + lead_fx >= step_fx / 2) { out_off(); early_off_done = true; }
  }
  if (!ext_mode && pos_fx >= step_fx) {
    pos_fx -= step_fx;
    seq_step_phase = 65535;
    const seq_pattern_t *pat = &seq_patterns[seq_pattern_cur];
    int len = pat->length ? pat->length : SEQ_STEPS;
    step++;
    if (step >= len) {
      step = 0;
      pattern_next();                        // queued pattern change / chain happens here
    }
    need_step_start = true;
  }
}
