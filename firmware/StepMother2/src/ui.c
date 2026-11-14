// Panel behaviour.
//
// NOTE PAGE (press Note)
//   Pads 1-13  C..C (shifted by Oct-/Oct+). Tapping one selects it as the note
//              to place and plays it. Dimmed pads (not in the scale) give the
//              nearest scale note instead (exactly in between: the one above).
//   Double-tap Note: record (Note LED blinks fast while recording, slowly while
//              armed and stopped). Pads you play go into the pattern, each on
//              the nearest step (fills only the steps played); holding a note
//              more than half way into the next step records a tie. Tap Note,
//              or go to Pattern / Function, to stop recording.
//   Poly pattern: pads pressed together (within 40 ms) form a chord of up to
//              4 notes; it plays, is selected and is what gets recorded / placed.
//   Pad 14     Scale. Hold + pads 1-6: chromatic, minor, major, minor
//              pentatonic, phrygian, blues. Hold + knob: root (C..B, absolute).
//              0.3.2: programmed notes follow a scale change (back again: as written).
//   Pad 15/16  Oct- / Oct+.
//
// PATTERN PAGE (press Pattern)
//   Tap a pad          empty step: place the selected note / chord. Lit step: clear it.
//   Hold a pad + knob  move that step's note (or whole chord) up/down the scale.
//                      Chord steps light a little brighter than single notes.
//   Accent + tap / Glide + tap / Accent + Glide + tap: accent / slide / tie.
//   Hold Pattern + tap select pattern 1-16 (ends a chain).
//   Double-tap Pattern: chain entry (Pattern blinks). Tap slots in order
//                      (repeats allowed), then tap Pattern, Note or Function to
//                      finish: the chain plays in that order and the Pattern
//                      page follows the pattern that's playing. (1.2.0, as on
//                      StepBro; before, slots were tapped while holding Pattern.)
//   Hold Pattern + Function + tap a slot: erase that pattern (notes and settings).
//   Hold Pattern + Glide: tap the source slot (blinks), then the destination:
//                      copy; the copy becomes the pattern you edit and play.
//   Hold Pattern + turn the knob: transpose the pattern (-12..+12, relative).
//
// FUNCTION PAGE (tap Function; tap again, or press Note / Pattern, to leave)
//   Tap a pad to select what the knob changes (stays selected; Volume when
//   you enter the page: Cutoff). Pickup: the value only follows once the knob passes
//   it. While turning, the pads show the value (a bar; Tune from the middle),
//   plus a blinking knob marker until picked up.
//     1 Cutoff  2 Resonance  3 Env Mod  4 Decay
//     5 Accent  6 Delay Amount  7 Delay Time (1/16 .. 1/4, pads 1-6 while turning)
//     8 Distortion (hold + pad 1/2: soft clip / sine)
//     9 Random: knob = density; tap again (while selected) to roll a pattern
//     10 Tune (+-7, centre = in tune)  11 Swing  12 Pattern Length
//     13 MIDI Channel (only the channel's pad lit; the knob picks it)
//     15 MIDI Lead (shown scrolling in ms; the knob sets it, 1 ms a step)
//   Tap to toggle: 14 MIDI out (dim = on, the default; dark = off)
//                  16 Mono/Poly (lit = Poly)
//   Double-tap 1-5, 9-11: record that knob into the pattern (pad blinks; tap to
//   stop). Function + pad: clear it. Recorded pads glow faintly.
//   With MIDI out on, knob moves are also sent as CC (and CCs are received).
//
// SYNC: always the DAW's MIDI clock, start, stop and song position (0.3.4:
//   no internal clock, tempo or metronome). Play mutes / unmutes the pattern
//   on the beat, also while the DAW is stopped.
//
// EVERYWHERE
//   Play: mute/unmute. Knob alone: cutoff (pickup). Hold Wave + knob: volume.
//   Tap Wave: saw/square. Function + Play: CPU load as a MIDI note on ch 16.
//   Everything is saved to flash ~1.5 s after the last change, while stopped.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "merged.h"
#include "persist.h"
#include "telemetry.h"
#include <stddef.h>
static bool panel_active;
#include "panel.h"
#include "audio.h"
#include "seq303.h"
#include "storage.h"
#include "pico/stdlib.h"

#define REC_LATENCY_MS    20.0f   // board -> computer -> your ears, roughly
#define KNOB_PER_DEGREE   150     // knob travel per scale note when editing a step
#define KNOB_ACTIVATE     60      // knob travel before an absolute selection takes over
#define SAVE_SETTLE_MS    1500    // save this long after the last change (when stopped)
#define CHORD_MS          40      // pads pressed this close together form one chord
#define DOUBLE_TAP_MS     300     // Note double-tap: record

#define PAD_SCALE   13
#define PAD_OCT_DN  14
#define PAD_OCT_UP  15

enum {  // Function page pads
  FP_CUTOFF, FP_RES, FP_ENVMOD, FP_DECAY,
  FP_ACCENT, FP_DAMOUNT, FP_DTIME, FP_DIST,
  FP_RANDOM, FP_TUNE, FP_SWING, FP_LENGTH,
  FP_CHANNEL, FP_MIDIOUT, FP_LEAD, FP_POLY,         // 0.3.4: Tempo/Clock gone; 11 = Swing, 13-15 = MIDI
  FP_VOLUME,          // not on a pad: Wave + knob
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static voice_params_t params = {
  .cutoff = 0.3f, .resonance = 0.7f, .envmod = 0.6f, .decay = 0.4f,
  .accent = 0.7f, .tune = 0.5f, .volume = 0.8f, .distortion = 0.0f, .wave = WAVE_SAW,
  .dist_drive = 0.5f, .dist_type = 0, .delay_time = 0.75f, .delay_amount = 0.0f,                    // delay 1/8D, off
};
static bool params_dirty = true;
static float stored_bpm=125.0f;

typedef enum { PAGE_NOTE, PAGE_PATTERN, PAGE_FUNCTION } page_t;

static page_t      page = PAGE_PATTERN, page_before_function = PAGE_PATTERN;
static int         entry_oct;                      // -1, 0, +1
static int         selected_pitch = 45;            // A2 (lowest note of the selection)
static uint8_t     sel_notes[SEQ_MAX_NOTES] = {45};   // selected note / chord, as heard
static int         sel_n = 1;
static bool        rec_armed;                      // Note double-tapped: recording
static uint32_t    note_tap_ms;
static uint8_t     edit_slot;
static bool        playing;
static seq_scale_t scale = SCALE_MINOR;
static int         root = 9;                       // A
static uint8_t     midi_channel;                   // 0..15
static uint8_t     clock_usb = 1;                  // follow USB MIDI clock (0.3.4: always)
static uint8_t     midi_out = 0;                   // send sequencer and pad notes over MIDI (0.3.6: off by default)
static uint8_t     midi_lead = 8;                  // synced: MIDI notes this many ms ahead (0..15; 0.3.5: was x 4 ms)
#define MIDI_LEAD_STEP_MS 1
#define MIDI_LEAD_DEFAULT 2                         // 8 ms: Ableton on a typical setup
static uint8_t     metro;                          // unused since 0.3.4 (no metronome)
static bool wave_was_held, wave_turned;
static int wave_start_knob;
static bool        wave_combo;                     // Wave used with Function: no saw/square toggle

// External clock state (core 0)
static bool     ext_running, ext_first_tick;
static uint32_t ext_ticks;                         // song position in clock ticks (24 per beat)
static uint32_t ext_spp;                           // last Song Position Pointer, in 16ths
static uint32_t ext_last_tick_us;
static float    ext_tick_us = 20833.0f;            // smoothed tick spacing (120 BPM)
static uint32_t fn_press_ms;

// Pattern page: a held step (for knob pitch editing / tap-to-toggle)
static int  held_step = -1;
static bool held_moved;            // the knob was turned while holding: release never clears
static bool held_sounding;         // the held step's note is playing (stopped: audition)
static uint32_t held_press_ms;     // a short tap clears / places; a longer hold never does
#define STEP_TAP_MS 400
#define FN_TAP_MS   500    // Clock / MIDI pads on the Function page: shorter = tap
static int  held_knob_start;
static uint8_t held_base[SEQ_MAX_NOTES], held_last[SEQ_MAX_NOTES];
static int  held_n;

// Note page: Scale pad held
static bool     scale_held;
static uint8_t  pad_sounding[13];                  // note each held note pad is playing
static int      scale_knob_start;
static bool     root_knob_active;
static uint32_t root_shown_until;

// Function key / page
static bool     fn_key_used;                       // Function used as a modifier: no page toggle
static int      fn_pad = -1;                       // Function-page pad being held (Clock, Channel, Copy, Poly)
static int      fn_sel = 0;                        // knob parameter selected by a tap (FP_CUTOFF)
static int      fn_sel_knob_start;                 // Length / Transpose: knob position when selected
static bool     fn_sel_active;
static uint32_t fn_turn_ms;                        // last knob movement (value shown for a second)
static int      fn_knob_start;
static bool     fn_knob_active;
static int      copy_flash_pad = -1;
static uint32_t copy_flash_until;

// Live recording of the note / chord currently held (for ties)
static uint32_t rec_pads;                          // pads of it still held
static int rec_step, rec_ties;

// Hold Pattern + Glide: copy (source, then destination). Hold Pattern + knob: transpose.
static int      copy_src = -1;
static int      tr_knob_start, tr_base;
static bool     tr_active;
static uint32_t tr_turn_ms;

// Pattern chain: slots tapped while Pattern is held
static uint8_t  chain_build[SEQ_CHAIN_MAX];
static int      chain_build_n;
static bool     chain_entry;                       // 1.2.0: double-tap Pattern, tap slots, finish
static uint32_t pattern_tap_ms;                    // for the Pattern double-tap
static uint8_t  ui_chain[SEQ_CHAIN_MAX];
static int      ui_chain_len;                      // 0 = no chain

// Note page chord grouping
static uint8_t  group[SEQ_MAX_NOTES];
static int      group_n;
static uint32_t group_ms;

static bool     usb_mounted, crashed;
static uint32_t last_underruns, flash_until;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

void ui_status(bool mounted, bool streaming, bool crash, uint32_t underruns) {
  usb_mounted = mounted;
  crashed = crash;
  if (streaming && underruns != last_underruns) flash_until = now_ms() + 150;
  last_underruns = underruns;
}

static seq_pattern_t *pat(void) { return &seq_patterns[edit_slot]; }
static int pat_len(void) { return pat()->length ? pat()->length : SEQ_STEPS; }
static int pad_pitch(int pad) { return SEQ_BASE_NOTE + pad + 12 * entry_oct; }
static bool poly(void) { return pat()->poly; }
static bool in_notes(const uint8_t *set, int n, int x) {
  for (int i = 0; i < n; i++) if (set[i] == x) return true;
  return false;
}
static void select_notes(const uint8_t *notes, int n) {
  for (int i = 0; i < n; i++) sel_notes[i] = notes[i];
  sel_n = n;
  int lo = notes[0];
  for (int i = 1; i < n; i++) if (notes[i] < lo) lo = notes[i];
  selected_pitch = lo;
}

// Steps store pitch relative to the reference key and without the pattern's
// transpose; these give / take the pitch you actually hear.
static void set_step_pitch(seq_step_t *s, int pitch) {
  s->pitch = (uint8_t)(pitch - seq_key_shift - pat()->transpose);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------------------------------------------------------------------------
// Knob pickup: a value only starts following the knob once the knob has
// passed it (or is within 2% of it), so nothing jumps when you grab the knob.
// ---------------------------------------------------------------------------

static int   knob_target = -1;       // what the knob currently controls
static bool  knob_picked;
static float knob_prev, knob_last_set;

// Returns true and the new value in *out when the value should change.
static bool knob_pickup(int target, float value, int knob, float *out) {
  float kv = knob / 4095.0f;
  if (target != knob_target) {                       // new destination: not picked up yet
    knob_target = target;
    knob_picked = false;
    knob_prev = kv;
  }
  if (knob_picked && fabsf(value - knob_last_set) > 0.04f) knob_picked = false;  // changed elsewhere (MIDI)
  if (!knob_picked) {
    if (fabsf(kv - value) < 0.02f || (kv - value) * (knob_prev - value) < 0) knob_picked = true;
    knob_prev = kv;
    if (!knob_picked) return false;
  }
  if (fabsf(kv - knob_last_set) < 0.003f) return false;   // knob not moving (or ADC noise): leave the value alone
  knob_last_set = kv;
  *out = kv;
  return true;
}

static void knob_release_target(void) { knob_target = -1; }

// ---------------------------------------------------------------------------
// Function-page values (0..1 for the knob parameters)
// ---------------------------------------------------------------------------

static float rnd_density = 0.5f;                   // Random: how busy a rolled pattern is
static bool fn_is_knob_param(int fp) {
  return fp <= FP_DIST || fp == FP_RANDOM || fp == FP_TUNE || fp == FP_SWING;
}

static float fn_get(int fp) {
  switch (fp) {
    case FP_CUTOFF: return params.cutoff;
    case FP_RES:    return params.resonance;
    case FP_ENVMOD: return params.envmod;
    case FP_DECAY:  return params.decay;
    case FP_ACCENT: return params.accent;
    case FP_TUNE:   return params.tune;
    case FP_RANDOM: return rnd_density;
    case FP_VOLUME: return params.volume;
    case FP_DIST:   return params.distortion;
    case FP_DTIME:  return params.delay_time;
    case FP_DAMOUNT: return params.delay_amount;
    case FP_SWING:  return pat()->swing / 100.0f;                       // 50..75 %
    default:        return 0;
  }
}

static void fn_set(int fp, float v) {
  switch (fp) {
    case FP_CUTOFF: params.cutoff = v; break;
    case FP_RES:    params.resonance = v; break;
    case FP_ENVMOD: params.envmod = v; break;
    case FP_DECAY:  params.decay = v; break;
    case FP_ACCENT: params.accent = v; break;
    case FP_TUNE:   params.tune = fabsf(v - 0.5f) < 0.03f ? 0.5f : v; break;   // easy to land in tune
    case FP_RANDOM: rnd_density = v; return;
    case FP_VOLUME: params.volume = v; break;
    case FP_DIST:   params.distortion = v; break;
    case FP_DTIME:  params.delay_time = v; break;
    case FP_DAMOUNT: params.delay_amount = v; break;
    case FP_SWING:  pat()->swing = (uint8_t)(v * 100.0f + 0.5f); return;
    default: return;
  }
  params_dirty = true;
}

// ---------------------------------------------------------------------------
// MIDI CC: the knob's moves go out (when MIDI out is on) and come back in.
// ---------------------------------------------------------------------------

static int8_t fp_cc(int fp) {
  switch (fp) {
    case FP_CUTOFF: return 74;  case FP_RES: return 71;  case FP_ENVMOD: return 76;
    case FP_DECAY: return 75;   case FP_ACCENT: return 77; case FP_TUNE: return 94;
    case FP_VOLUME: return 7;   case FP_DIST: return 78;  case FP_DTIME: return 12;
    case FP_DAMOUNT: return 13; default: return -1;
  }
}
#define CC_DIST_TYPE 14

static void cc_out(int cc, int value) {
  static int8_t last[128];
  static bool init;
  if (!init) { memset(last, -1, sizeof(last)); init = true; }
  if (!midi_out || cc < 0 || last[cc] == value) return;   // only when the 7-bit value changes
  last[cc] = (int8_t)value;
  midi_send((uint8_t)(0xB0 | midi_channel), (uint8_t)cc, (uint8_t)value);
}

// ---------------------------------------------------------------------------
// Recorded knob movements: Cutoff, Resonance, Env Mod, Decay, Accent, Delay
// Time, Delay Amount and Distortion, one value per step and pattern.
// Double-tap the parameter's Function pad to record (pad blinks); while the
// pattern plays, knob moves are written into the steps they pass. Playback
// glides from step to step (Delay Time jumps). Turning the knob takes over
// live until the pattern loops. Function + pad clears it for the pattern.
// ---------------------------------------------------------------------------

static uint8_t  autom[SEQ_PATTERNS][AUTO_PARAMS][SEQ_STEPS];
static const int8_t AUTO_FP[AUTO_PARAMS] = { FP_CUTOFF, FP_RES, FP_ENVMOD, FP_DECAY, FP_ACCENT, FP_DTIME, FP_DAMOUNT, FP_DIST };
static int      auto_armed = -1;                   // parameter being recorded
static int      auto_tap_pad = -1;                 // double-tap detection
static uint32_t auto_tap_ms;
static uint32_t auto_moved_ms[AUTO_PARAMS];        // last knob move of each
static bool     auto_live[AUTO_PARAMS];            // knob took over until the pattern loops
static int      auto_last_pat = -1, auto_last_step = -1;

static int auto_index(int fp) {
  for (int i = 0; i < AUTO_PARAMS; i++) if (AUTO_FP[i] == fp) return i;
  return -1;
}

static bool auto_has(int slot, int a) {
  for (int k = 0; k < SEQ_STEPS; k++) if (autom[slot][a][k] != AUTO_NONE) return true;
  return false;
}

// A parameter changed by the knob (any page): send it as CC, and take over
// from any recording of it until the pattern loops.
static void knob_set(int fp, float v) {
  fn_set(fp, v);
  int a = auto_index(fp);
  if (a >= 0) { auto_live[a] = true; auto_moved_ms[a] = now_ms(); }
  cc_out(fp_cc(fp), (int)(fn_get(fp) * 127.0f + 0.5f));
}

static void auto_task(void) {
  if (!playing || seq_playhead < 0) { auto_last_step = -1; auto_last_pat = -1; return; }
  int p = seq_pattern_cur, st = seq_playhead;
  if (st >= SEQ_STEPS) return;
  // pattern looped or changed: live takeovers end, recordings play again
  if (auto_last_pat >= 0 && (p != auto_last_pat || st < auto_last_step)) memset(auto_live, 0, sizeof(auto_live));
  auto_last_pat = p; auto_last_step = st;
  uint32_t now = now_ms();
  if (auto_armed >= 0 && now - auto_moved_ms[auto_armed] < 150)          // recording: the step being passed
    autom[p][auto_armed][st] = (uint8_t)(fn_get(AUTO_FP[auto_armed]) * 254.0f + 0.5f);
  int len = seq_patterns[p].length ? seq_patterns[p].length : SEQ_STEPS;
  float ph = seq_step_phase / 65535.0f;
  for (int a = 0; a < AUTO_PARAMS; a++) {
    if (auto_live[a]) continue;
    uint8_t v0 = autom[p][a][st];
    if (v0 == AUTO_NONE) continue;
    float v = v0 / 254.0f;
    if (AUTO_FP[a] != FP_DTIME) {                                         // glide towards the next step
      uint8_t v1 = autom[p][a][(st + 1) % len];
      if (v1 != AUTO_NONE) v += (v1 / 254.0f - v) * ph;
    }
    if (fabsf(fn_get(AUTO_FP[a]) - v) > 0.002f) fn_set(AUTO_FP[a], v);
  }
}

// ---------------------------------------------------------------------------
// Live notes (pads, MIDI in): last pressed wins; releasing it slides back to
// the previous held note.
// ---------------------------------------------------------------------------

#define MAX_HELD 16
static uint8_t held[MAX_HELD];
static int n_held;

static void held_remove(uint8_t note) {
  for (int i = 0; i < n_held; i++) {
    if (held[i] == note) {
      for (int j = i; j < n_held - 1 && j < MAX_HELD - 1; j++) held[j] = held[j + 1];
      n_held--;
      return;
    }
  }
}

// Poly patterns: the newest (up to) 4 held notes sound as a chord. A note
// within CHORD_MS of the first joins without restarting the envelopes; later
// notes restart them; releasing a note just drops it from the chord.
static uint32_t live_group_ms;

static void live_chord(voice_mode_t mode, bool accent) {
  int n = n_held < SEQ_MAX_NOTES ? n_held : SEQ_MAX_NOTES;
  audio_chord_on(&held[n_held - n], n, accent, mode);
}

static void live_press(uint8_t note, bool accent, bool slide) {
  bool was_sounding = n_held > 0;
  held_remove(note);
  if (n_held < MAX_HELD) held[n_held++] = note;
  if (poly()) {
    uint32_t now = now_ms();
    bool joins = was_sounding && now - live_group_ms <= CHORD_MS;
    if (!joins) live_group_ms = now;
    live_chord(joins ? VOICE_LEGATO : VOICE_RETRIGGER, accent);
    return;
  }
  audio_note_on(note, accent, slide && was_sounding);
}

static void live_release(uint8_t note) {
  bool was_top = n_held > 0 && held[n_held - 1] == note;
  bool was_held = false;
  for (int i = 0; i < n_held; i++) if (held[i] == note) was_held = true;
  held_remove(note);
  if (poly()) {
    if (!was_held) return;
    if (n_held > 0) live_chord(VOICE_LEGATO, false);
    else audio_note_off();
    return;
  }
  if (!was_top) return;
  if (n_held > 0) audio_note_on(held[n_held - 1], false, true);
  else audio_note_off();
}

// ---------------------------------------------------------------------------
// MIDI in (only on the selected channel)
// ---------------------------------------------------------------------------

void ui_midi_in(uint8_t status, uint8_t channel, uint8_t d1, uint8_t d2) {
  if (channel != midi_channel) return;
  if (status == 0x90 && d2 > 0) { live_press(d1, d2 >= 100, true); return; }   // overlap = slide
  if (status == 0x80 || (status == 0x90 && d2 == 0)) { live_release(d1); return; }
  if (status != 0xB0) return;
  if (d1 == 120 || d1 == 123) {                       // all sound / all notes off
    if (n_held) { n_held = 0; audio_note_off(); }
    return;
  }
  float v = d2 / 127.0f;
  switch (d1) {
    case 74: params.cutoff = v; break;
    case 71: params.resonance = v; break;
    case 76: params.envmod = v; break;
    case 75: params.decay = v; break;
    case 77: params.accent = v; break;
    case 78: params.distortion = v; break;
    case 7:  params.volume = v; break;
    case 94: params.tune = v; break;
    case 12: params.delay_time = v; break;
    case 13: params.delay_amount = v; break;
    case CC_DIST_TYPE: params.dist_type = d2 >= 64; break;
    default: return;
  }
  params_dirty = true;
}

// MIDI clock and transport from the DAW (only when Clock Source = USB).
// 24 clock ticks per beat = 6 per 16th step. Song Position counts 16ths.
void ui_midi_system(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!clock_usb) return;
  switch (status) {
    case 0xF2:                                        // Song Position Pointer
      ext_spp = (uint32_t)d1 | ((uint32_t)d2 << 7);
      break;
    case 0xFA:                                        // Start (from the top)
      ext_spp = 0;
      /* fall through */
    case 0xFB:                                        // Continue (from the song position)
      ext_ticks = ext_spp * 6;
      ext_running = true;
      ext_first_tick = true;
      seq_pattern_next = edit_slot;
      playing = true;
      audio_seq_ext_start();
      break;
    case 0xFC:                                        // Stop
      if (ext_running || playing) { playing = false; audio_seq(false); }
      ext_running = false;
      break;
    case 0xF8: {                                      // Clock tick
      uint32_t now = time_us_32();
      uint32_t dt = now - ext_last_tick_us;
      ext_last_tick_us = now;
      if (dt > 2000 && dt < 100000) {                 // 25..1250 BPM: a real tick interval
        ext_tick_us += (dt - ext_tick_us) * 0.1f;
        seq_bpm = 60000000.0f / (ext_tick_us * 24.0f);
      }
      if (!ext_running) break;
      ext_first_tick = false;
      if (ext_ticks % 6 == 0) audio_ext_step(ext_ticks / 6);
      ext_ticks++;
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Note page
// ---------------------------------------------------------------------------

// MIDI out of the pads (when MIDI out is on): every note you play goes to the
// computer too, playing or not, mixed with the sequencer's notes. A note two
// pads both snap to is sent once, and ends when the last of them is released.
// 0.3.2: every Yummy note to MIDI goes through here. Shift+Mute pad 2 on the
// StepBro level mutes them; notes already sounding get their note-off when the
// mute starts, so nothing hangs in the DAW. CCs are not muted.
// Rarely-run code is kept small and out of line: on the RP2040 the whole
// program runs from RAM, which is tight.
#define YUMMY_COLD __attribute__((noinline, optimize("Os")))
static bool    midi_muted, audio_muted;
static uint8_t notes_on[128];                      // note-ons sent and not yet closed
static void note_out(uint8_t note, uint8_t vel) {
  note &= 127;
  if (vel) {
    if (midi_muted) return;
    if (notes_on[note] < 255) notes_on[note]++;
    midi_send((uint8_t)(0x90 | midi_channel), note, vel);
  } else {
    if (!notes_on[note]) return;                    // never sent (muted): nothing to close
    notes_on[note]--;
    midi_send((uint8_t)(0x80 | midi_channel), note, 0);
  }
}
YUMMY_COLD static void notes_close_all(void) {
  for (int n = 0; n < 128; n++) while (notes_on[n]) { notes_on[n]--; midi_send((uint8_t)(0x80 | midi_channel), (uint8_t)n, 0); }
}
void ui_yummy_midi_mute_toggle(void) { midi_muted = !midi_muted; if (midi_muted) notes_close_all(); }
void ui_yummy_audio_mute_toggle(void) { audio_muted = !audio_muted; audio_mute(audio_muted); }
bool ui_yummy_midi_muted(void) { return midi_muted; }
bool ui_yummy_audio_muted(void) { return audio_muted; }

// 0.3.2: changing the scale moves every programmed note to the same place in
// the new scale (the 3rd note of Minor becomes the 3rd note of Major; between
// scales of different sizes, the same proportion of the way up the octave).
// Each change is worked out from the melody as last edited, so flipping
// through scales and back restores it exactly. Editing a note (or a reboot)
// makes the current notes the new original. Chromatic leaves notes as they are.
static uint8_t     orig_notes[SEQ_PATTERNS][SEQ_STEPS][4];   // stored pitch + chord
static seq_scale_t orig_scale;
static bool        orig_valid;
static uint32_t    orig_check;                     // notes as last written by a change
YUMMY_COLD static uint32_t notes_check(void) {
  uint32_t h = 2166136261u;                        // FNV-1a over everything that counts as an edit
  for (int i = 0; i < SEQ_PATTERNS; i++) for (int st = 0; st < SEQ_STEPS; st++) {
    const seq_step_t *x = &seq_patterns[i].steps[st];
    uint8_t b[5] = {x->pitch, x->chord[0], x->chord[1], x->chord[2], x->on};
    for (int k = 0; k < 5; k++) { h ^= b[k]; h *= 16777619u; }
  }
  return h;
}
YUMMY_COLD static int scale_pcs(seq_scale_t sc, uint8_t *pcs) {
  int n = 0;
  for (int pc = 0; pc < 12; pc++) if (seq_in_scale(root + pc, sc, root)) pcs[n++] = (uint8_t)pc;
  return n;
}
YUMMY_COLD static int remap_pitch(int pitch, seq_scale_t from, seq_scale_t to) {
  if (from == to || to == SCALE_CHROMATIC) return pitch;
  if (from == SCALE_CHROMATIC) return seq_scale_snap(pitch, to, root);
  pitch = seq_scale_snap(pitch, from, root);
  uint8_t a[12], b[12];
  int na = scale_pcs(from, a), nb = scale_pcs(to, b);
  int rel = pitch - root, oct = rel >= 0 ? rel / 12 : -((11 - rel) / 12), pc = rel - oct * 12;
  int d = 0;
  while (d < na - 1 && a[d] != pc) d++;
  int e = (2 * d * nb + na) / (2 * na);              // nearest, same share of the octave
  if (e >= nb) { e -= nb; oct++; }
  int out = root + oct * 12 + b[e];
  return out < 0 ? 0 : out > 127 ? 127 : out;
}
YUMMY_COLD static void set_scale(seq_scale_t to) {
  if (to == scale) return;
  if (!orig_valid || notes_check() != orig_check) {   // edited since: this is the new original
    for (int i = 0; i < SEQ_PATTERNS; i++) for (int st = 0; st < SEQ_STEPS; st++) {
      const seq_step_t *x = &seq_patterns[i].steps[st];
      uint8_t *o = orig_notes[i][st];
      o[0] = x->pitch; o[1] = x->chord[0]; o[2] = x->chord[1]; o[3] = x->chord[2];
    }
    orig_scale = scale; orig_valid = true;
  }
  for (int i = 0; i < SEQ_PATTERNS; i++) {
    seq_pattern_t *pt = &seq_patterns[i];
    for (int st = 0; st < SEQ_STEPS; st++) {
      seq_step_t *x = &pt->steps[st];
      const uint8_t *o = orig_notes[i][st];
      if (!x->on) continue;                           // rests keep their (unused) pitch
      seq_step_t from = *x;
      from.pitch = o[0]; from.chord[0] = o[1]; from.chord[1] = o[2]; from.chord[2] = o[3];
      if (to == orig_scale) { *x = from; continue; }   // back where it was written: exact
      uint8_t notes[SEQ_MAX_NOTES], out[SEQ_MAX_NOTES];
      int n = seq_step_get(pt, &from, notes), m = 0;
      for (int k = 0; k < n; k++) {
        uint8_t q = (uint8_t)remap_pitch(notes[k], orig_scale, to);
        bool dup = false;
        for (int j = 0; j < m; j++) if (out[j] == q) dup = true;
        if (!dup) out[m++] = q;
      }
      seq_step_set(pt, x, out, m);
    }
  }
  scale = to;
  orig_check = notes_check();
}

static bool pad_note_held(int note, int except) {
  for (int i = 0; i <= 12; i++) if (i != except && pad_sounding[i] == note) return true;
  return false;
}

static void note_page_press(int pad, bool recording) {
  if (pad == PAD_SCALE) {
    scale_held = true;
    scale_knob_start = panel_knob();
    root_knob_active = false;
    return;
  }
  if (scale_held) {                                   // Scale + pad 1-6: choose scale
    if (pad < SCALE_COUNT) set_scale((seq_scale_t)pad);
    return;
  }
  if (pad == PAD_OCT_DN) { if (entry_oct > -1) entry_oct--; return; }
  if (pad == PAD_OCT_UP) { if (entry_oct < 1) entry_oct++; return; }

  // Dimmed (out-of-scale) pads play the nearest scale note instead.
  int pitch = seq_scale_snap(pad_pitch(pad), scale, root);
  if (midi_out && !pad_note_held(pitch, pad)) note_out((uint8_t)pitch, 100);
  pad_sounding[pad] = (uint8_t)pitch;

  // Chords (poly patterns): pads pressed within CHORD_MS of the first join it.
  uint32_t now = now_ms();
  bool joins = poly() && group_n > 0 && group_n < SEQ_MAX_NOTES && now - group_ms <= CHORD_MS;
  if (!joins) { group_n = 0; group_ms = now; }
  bool dup = in_notes(group, group_n, pitch);        // two pads snapped to the same note
  if (!dup) group[group_n++] = (uint8_t)pitch;
  select_notes(group, group_n);
  live_press((uint8_t)pitch, false, false);

  if (recording) {
    if (joins && rec_pads) {                          // add to the chord just recorded
      if (!dup) {
        seq_step_t *s = &pat()->steps[rec_step];
        uint8_t notes[SEQ_MAX_NOTES];
        int n = seq_step_get(pat(), s, notes);
        if (n < SEQ_MAX_NOTES && !in_notes(notes, n, pitch)) { notes[n++] = (uint8_t)pitch; seq_step_set(pat(), s, notes, n); }
      }
      rec_pads |= 1u << pad;
    } else {
      int idx = seq_record_step(seq_playhead, seq_step_phase, pat_len(), seq_bpm, REC_LATENCY_MS);
      seq_step_t *s = &pat()->steps[idx];
      uint8_t one = (uint8_t)pitch;
      seq_step_set(pat(), s, &one, 1);
      s->tie = 0;
      s->on = 1;                                      // accent / slide on that step are kept
      rec_pads = 1u << pad; rec_step = idx; rec_ties = 0;   // holding on records ties
    }
  }
}

static void note_page_release(int pad) {
  rec_pads &= ~(1u << pad);
  if (pad <= 12 && pad_sounding[pad] && midi_out && !pad_note_held(pad_sounding[pad], pad))
    note_out(pad_sounding[pad], 0);
  if (pad == PAD_SCALE) { scale_held = false; return; }
  if (pad <= 12 && pad_sounding[pad]) { live_release(pad_sounding[pad]); pad_sounding[pad] = 0; }
}

// While a recorded note is still held, each step it is held well into
// (past half of it) is recorded as a tie.
static void record_ties(void) {
  bool recording = rec_armed && playing && page == PAGE_NOTE;
  if (!recording) { rec_pads = 0; return; }
  if (!rec_pads || seq_playhead < 0) return;
  int len = pat_len();
  if (rec_ties >= len - 1) return;
  float step_ms = 60000.0f / seq_bpm / 4.0f;
  // nearest step to (now - 1 step): becomes the step after rec_step once we're
  // more than half way into that step
  int k = seq_record_step(seq_playhead, seq_step_phase, len, seq_bpm, REC_LATENCY_MS + step_ms);
  int next = (rec_step + 1) % len;
  if (k == next) {
    seq_step_t *s = &pat()->steps[next];
    s->on = 1; s->tie = 1; s->accent = 0; s->slide = 0;
    rec_step = next;
    rec_ties++;
  }
}

// ---------------------------------------------------------------------------
// Pattern page
// ---------------------------------------------------------------------------

// A tie extends the note before it, so it can only go on an empty step that
// follows a note or another tie (wrapping round the end of the pattern).
static bool tie_allowed(int i) {
  const seq_step_t *s = &pat()->steps[i];
  const seq_step_t *prev = &pat()->steps[(i - 1 + pat_len()) % pat_len()];
  return !s->on && prev->on;
}

static void clear_step(seq_step_t *s) {
  s->on = 0; s->accent = 0; s->slide = 0; s->tie = 0;
  s->chord[0] = s->chord[1] = s->chord[2] = 0;
}

static void pattern_page_press(int pad, bool accent, bool glide) {
  seq_step_t *s = &pat()->steps[pad];
  if (accent && glide) {                              // tie: only on an empty step after a note/tie
    if (s->on && s->tie) clear_step(s);
    else if (tie_allowed(pad)) { s->on = 1; s->tie = 1; s->accent = 0; s->slide = 0; }
    return;
  }
  if (accent) { if (s->on && !s->tie) s->accent = !s->accent; return; }
  if (glide)  { if (s->on) s->slide = !s->slide; return; }

  if (held_step < 0) {                                // tap or hold-to-edit decided on release
    held_step = pad;
    held_moved = false;
    held_press_ms = now_ms();
    held_knob_start = panel_knob();
    if (s->on && !s->tie) held_n = seq_step_notes(pat(), s, held_base);   // mono pattern: lowest only
    else { held_n = poly() ? sel_n : 1; memcpy(held_base, sel_notes, sizeof(held_base)); if (!poly()) held_base[0] = (uint8_t)selected_pitch; }
    memcpy(held_last, held_base, sizeof(held_last));
    // Stopped: a step with a note plays it at once, for as long as it's held.
    held_sounding = !playing && s->on && !s->tie;
    if (held_sounding) audio_chord_on(held_base, held_n, false, VOICE_RETRIGGER);
  }
}

static void pattern_page_release(int pad) {
  if (pad != held_step) return;
  seq_step_t *s = &pat()->steps[pad];
  if (!held_moved && now_ms() - held_press_ms < STEP_TAP_MS) {   // a tap: clear / place
    if (s->on) clear_step(s);
    else {
      s->on = 1; s->tie = 0; s->accent = 0; s->slide = 0;
      uint8_t one = (uint8_t)selected_pitch;
      if (poly()) seq_step_set(pat(), s, sel_notes, sel_n);
      else seq_step_set(pat(), s, &one, 1);
    }
  }
  if (held_sounding) audio_note_off();               // end the audition
  held_sounding = false;
  held_step = -1;
}

// Hold a step and turn: move its note through the scale. Only while stopped,
// and you hear each note as you reach it; while playing the knob does nothing
// here (but still counts as turned, so letting go doesn't clear the step).
static void edit_held_step(int knob) {
  int delta = knob - held_knob_start;
  if (abs(delta) > KNOB_ACTIVATE / 2) held_moved = true;
  if (playing) return;
  int degrees = delta / KNOB_PER_DEGREE;
  if (degrees == 0 && memcmp(held_last, held_base, (size_t)held_n) == 0) return;   // nothing to change
  uint8_t notes[SEQ_MAX_NOTES];                       // the whole chord moves through the scale
  for (int i = 0; i < held_n; i++)
    notes[i] = (uint8_t)(degrees ? seq_scale_move(held_base[i], degrees, scale, root) : held_base[i]);
  seq_step_t *s = &pat()->steps[held_step];
  s->on = 1; s->tie = 0;
  if (poly()) seq_step_set(pat(), s, notes, held_n);
  else set_step_pitch(s, notes[0]);                   // mono: the lowest note; chord notes are kept
  bool changed = memcmp(notes, held_last, (size_t)held_n) != 0;
  if (changed) { audio_chord_on(notes, held_n, false, VOICE_RETRIGGER); held_sounding = true; }   // hear it
  memcpy(held_last, notes, (size_t)held_n);
}

// ---------------------------------------------------------------------------
// Function page
// ---------------------------------------------------------------------------

static void set_chain(const uint8_t *c, int n);

static void copy_pattern(int src, int dest) {
  if (dest < 0 || dest >= SEQ_PATTERNS || src < 0 || src >= SEQ_PATTERNS || dest == src) return;
  seq_patterns[dest] = seq_patterns[src];
  memcpy(autom[dest], autom[src], sizeof(autom[dest]));
  copy_flash_pad = dest;
  copy_flash_until = now_ms() + 400;
  // The copy becomes the pattern you edit and play (switches at the end of
  // the current pattern while playing, which can't be heard: it's identical).
  edit_slot = (uint8_t)dest;
  seq_pattern_next = edit_slot;
  set_chain(NULL, 0);
}

// Pattern + knob transpose with a detent at 0: one semitone per TR_STEP of
// knob travel, and 0 is held over TR_ZERO (wider), so it's easy to land on.
#define TR_STEP  (4096 / 25)
#define TR_ZERO  (TR_STEP * 3 / 4)          // half-width of the 0 zone
static int transpose_from_knob(int base, int delta) {
  // position of the starting transpose on the knob scale (middle of its step)
  int u = base == 0 ? 0 : (base > 0 ? 1 : -1) * (TR_ZERO + (abs(base) - 1) * TR_STEP + TR_STEP / 2);
  u += delta;
  if (abs(u) < TR_ZERO) return 0;
  int t = 1 + (abs(u) - TR_ZERO) / TR_STEP;
  if (t > 12) t = 12;
  return u > 0 ? t : -t;
}

static void set_transpose(int t) {
  if (t < -12) t = -12;
  if (t > 12) t = 12;
  pat()->transpose = (int8_t)t;                     // the whole pattern moves by t semitones
}

// Tap selects, then the knob sets: the knob parameters, Length, MIDI channel and MIDI lead.
static bool fn_is_latched(int fp) { return fn_is_knob_param(fp) || fp == FP_LENGTH || fp == FP_CHANNEL || fp == FP_LEAD; }

static void fn_select(int fp) {
  fn_sel = fp;
  knob_release_target();                             // pickup starts afresh
  fn_sel_knob_start = panel_knob();
  fn_sel_active = false;
}

// ---------------------------------------------------------------------------
// Random pattern (Function pad 9): tap to select (knob = density), tap again
// to roll a new 16-step pattern into the current slot, in the current scale
// and key. Swing, transpose, Mono/Poly and recorded knob moves are kept; in a
// poly pattern some notes become chords from the scale.
// ---------------------------------------------------------------------------

static uint32_t rnd_state;
static uint32_t rnd(void) {                          // xorshift32
  if (!rnd_state) rnd_state = time_us_32() | 1;
  rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5;
  return rnd_state;
}
static float rnd01(void) { return (rnd() >> 8) * (1.0f / 16777216.0f); }

static void random_pattern(void) {
  seq_pattern_t *p = pat();
  // scale degrees above the root, weighted towards root and fifth (a bassline)
  static const int8_t DEG[]  = { 0, 0, 0, 4, 4, 2, 1, 3, 5, 6, 7, -1 };
  int base = SEQ_BASE_NOTE + root;                   // root in the low octave (C2..B2)
  float dens = 0.2f + 0.65f * rnd_density;
  bool prev_note = false;
  p->length = SEQ_STEPS;
  for (int i = 0; i < SEQ_STEPS; i++) {
    seq_step_t *s = &p->steps[i];
    memset(s, 0, sizeof(*s));
    float pn = dens + ((i % 4) == 0 ? 0.2f : 0.0f);  // downbeats a bit more likely
    if (rnd01() < pn) {
      int deg = DEG[rnd() % (sizeof(DEG) / sizeof(DEG[0]))];
      int pitch = seq_scale_move(base, deg, scale, root);
      float o = rnd01();
      if (o < 0.15f) pitch += 12; else if (o < 0.2f) pitch -= 12;   // occasional octave jumps
      uint8_t notes[SEQ_MAX_NOTES] = { (uint8_t)pitch };
      int n = 1;
      if (p->poly && rnd01() < 0.35f) {              // chord from the scale: a triad
        notes[n++] = (uint8_t)seq_scale_move(pitch, 2, scale, root);
        notes[n++] = (uint8_t)seq_scale_move(pitch, 4, scale, root);
      }
      seq_step_set(p, s, notes, n);
      s->on = 1;
      s->accent = rnd01() < 0.25f;
      s->slide = rnd01() < 0.2f;
      prev_note = true;
    } else if (prev_note && rnd01() < 0.12f) {      // a tie after a note
      s->on = 1; s->tie = 1;
    } else {
      prev_note = false;
    }
  }
  copy_flash_pad = FP_RANDOM;                        // brief flash to confirm
  copy_flash_until = now_ms() + 300;
}

// Tempo shown as scrolling digits (3 pads wide, 4 tall), right to left.
static const uint8_t DIGITS[10][4] = {
  {7, 5, 5, 7}, {2, 6, 2, 7}, {6, 1, 2, 7}, {7, 3, 1, 7}, {5, 5, 7, 1},
  {7, 6, 1, 6}, {4, 7, 5, 7}, {7, 1, 2, 2}, {7, 7, 5, 7}, {7, 5, 7, 1},
};
#define TICK_MS 120                                  // per column

// MIDI lead shown the same way, in ms (Clock pad: after turning, or held)
static uint32_t lead_tick_start, lead_turn_ms;
static bool     lead_tick_on;

// Columns of the text (4 bits each, bit 3 = top row), with blank lead-in and out.
static int ticker_columns(int bpm, uint8_t *cols) {
  char txt[4]; int nd = 0;
  if (bpm >= 100) txt[nd++] = (char)(bpm / 100);
  txt[nd++] = (char)((bpm / 10) % 10);
  txt[nd++] = (char)(bpm % 10);
  int n = 0;
  for (int i = 0; i < 4; i++) cols[n++] = 0;
  for (int d = 0; d < nd; d++) {
    for (int c = 0; c < 3; c++) {
      uint8_t col = 0;
      for (int r = 0; r < 4; r++) if (DIGITS[(int)txt[d]][r] & (4 >> c)) col |= (uint8_t)(8 >> r);
      cols[n++] = col;
    }
    cols[n++] = 0;
  }
  for (int i = 0; i < 3; i++) cols[n++] = 0;
  return n;
}

static bool dist_drive_turned, dist_type_changed;
static uint32_t dist_press_ms;
static int dist_knob_start;
static bool dist_held;                               // Distortion pad held: pads 1/2 pick the type

static void function_page_press(int pad) {
  if (fn_pad < 0 && pad == FP_RANDOM && fn_sel == FP_RANDOM) { random_pattern(); return; }   // selected: roll
  if (dist_held) {                                    // Distortion held: 1 = soft clip, 2 = sine
    if (pad <= 1) { dist_type_changed = true; params.dist_type = (uint8_t)pad; params_dirty = true; cc_out(CC_DIST_TYPE, pad ? 127 : 0); }
    return;
  }
  int a = auto_index(pad);
  if (auto_armed >= 0 && a != auto_armed && fn_is_latched(pad)) auto_armed = -1;   // the knob moves on: stop recording
  if (fn_pad < 0 && a >= 0 && pad != FP_DIST) {                         // double-tap: record; tap while recording: stop
    uint32_t now = now_ms();
    if (auto_armed == a) { auto_armed = -1; auto_tap_pad = -1; }
    else if (auto_tap_pad == pad && now - auto_tap_ms <= DOUBLE_TAP_MS) { auto_armed = a; auto_tap_pad = -1; }
    else { auto_tap_pad = pad; auto_tap_ms = now; }
  }
  if (fn_pad < 0 && pad == FP_DIST) {
    dist_held = true;
    dist_drive_turned = false;
    dist_type_changed = false;
    dist_press_ms = now_ms();
    dist_knob_start = panel_knob();
    knob_release_target();
  }
  if (fn_pad < 0 && fn_is_latched(pad)) {             // knob parameter: tap selects it
    fn_select(pad);
    fn_turn_ms = now_ms();                            // show its value for a moment
    if (pad == FP_LEAD) { lead_tick_on = true; lead_tick_start = now_ms() + 300; }   // the lead, in ms
    return;
  }
  if (fn_pad < 0) {                                   // first pad: what we're editing
    fn_pad = pad;
    fn_knob_start = panel_knob();
    fn_knob_active = false;
    fn_press_ms = now_ms();
    if (pad == FP_POLY)  pat()->poly = !pat()->poly;
    return;
  }
}

static void function_page_release(int pad) {
  if (pad == FP_DIST) {
    dist_held = false;
    knob_release_target();
    if (dist_drive_turned || dist_type_changed) auto_tap_pad = -1;
    else if (now_ms() - dist_press_ms < FN_TAP_MS) {
      int a = auto_index(FP_DIST);
      uint32_t now = now_ms();
      if (auto_armed == a) { auto_armed = -1; auto_tap_pad = -1; }
      else if (auto_tap_pad == FP_DIST && now - auto_tap_ms <= DOUBLE_TAP_MS) {
        auto_armed = a; auto_tap_pad = -1;
      } else { auto_tap_pad = FP_DIST; auto_tap_ms = now; }
    }
  }
  if (pad != fn_pad) return;
  bool tap = !fn_knob_active && now_ms() - fn_press_ms < FN_TAP_MS;
  if (fn_pad == FP_MIDIOUT && tap) {                  // tap: MIDI out on (pad bright) / off (dark)
    if (midi_out) notes_close_all();                  // off: close what is sounding
    midi_out = !midi_out;
  }
  fn_pad = -1;
  knob_release_target();
}

// Knob while a tap-type Function pad (MIDI out, Mono/Poly) is held: nothing to
// set, but turning it means the press was not a tap.
static void function_knob_select(int knob) {
  if (!fn_knob_active && abs(knob - fn_knob_start) > KNOB_ACTIVATE) fn_knob_active = true;
}

// Knob on the Function page with nothing held: the selected parameter.
static void function_knob_latched(int knob) {
  static int last_knob = -1;
  if (last_knob < 0 || abs(knob - last_knob) > 24) { last_knob = knob; fn_turn_ms = now_ms(); }
  float v;
  if (fn_is_knob_param(fn_sel)) {
    if (knob_pickup(100 + fn_sel, fn_get(fn_sel), knob, &v)) {
      knob_set(fn_sel, v);
    }
    return;
  }
  // Length: absolute once the knob has moved
  if (!fn_sel_active && abs(knob - fn_sel_knob_start) > KNOB_ACTIVATE) fn_sel_active = true;
  if (!fn_sel_active) return;
  int i16 = knob * 16 / 4096;  if (i16 > 15) i16 = 15;
  if (fn_sel == FP_LENGTH) pat()->length = (uint8_t)(i16 + 1);
  if (fn_sel == FP_CHANNEL && midi_channel != (uint8_t)i16) {   // the only way to set it
    notes_close_all();                                // end notes on the old channel
    midi_channel = (uint8_t)i16;
  }
  if (fn_sel == FP_LEAD) {                            // MIDI ahead when synced, 1 ms a step
    if (midi_lead != i16 || !lead_turn_ms) lead_turn_ms = now_ms();
    midi_lead = (uint8_t)i16;
    lead_tick_on = true;
    lead_tick_start = lead_turn_ms + 500;             // the value once you stop turning
  }
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

static void set_chain(const uint8_t *c, int n) {
  if (n < 2 || !c) n = 0;
  for (int i = 0; i < n; i++) { ui_chain[i] = c[i]; seq_chain[i] = c[i]; }
  ui_chain_len = n;
  seq_chain_len = (uint8_t)n;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  seq_chain_version++;                               // the sequencer takes it at the end of the pattern
}

// 1.2.0: end chain entry: two or more slots make a chain, one slot plays on
// its own (ends a chain), none changes nothing.
YUMMY_COLD static void chain_finish(void) {
  if (!chain_entry) return;
  chain_entry = false;
  if (chain_build_n == 1) set_chain(chain_build, 0);
  else if (chain_build_n >= 2) {
    set_chain(chain_build, chain_build_n);
    if (!playing) edit_slot = chain_build[0];
  }
  chain_build_n = 0;
}

static void erase_pattern(int slot) {
  memset(&seq_patterns[slot], 0, sizeof(seq_patterns[slot]));
  memset(autom[slot], AUTO_NONE, sizeof(autom[slot]));
  seq_patterns[slot].length = SEQ_STEPS;
  copy_flash_pad = slot;                             // brief flash to confirm
  copy_flash_until = now_ms() + 400;
}

static void leave_page_holds(void) {
  held_step = -1;
  scale_held = false;
  dist_held = false;
  dist_drive_turned = false;
  fn_pad = -1;
  knob_release_target();
}

static void handle_keys(void) {
  uint32_t pressed  = merged_yummy_pressed();
  uint32_t released = merged_yummy_released();
  uint32_t keys     = merged_yummy_keys();
  bool accent   = keys & (1u << KEY_ACCENT);
  bool glide    = keys & (1u << KEY_GLIDE);
  bool function = keys & (1u << KEY_FUNCTION);
  bool pattern_held = keys & (1u << KEY_PATTERN);
  // Function: a tap toggles the Function page; used together with another key
  // (Play, Accent for the bootloader) it's a modifier and doesn't toggle.
  if (pressed & (1u << KEY_FUNCTION)) fn_key_used = false;
  if (function && (pressed & ~(1u << KEY_FUNCTION))) fn_key_used = true;
  if ((released & (1u << KEY_FUNCTION)) && !fn_key_used) {
    chain_finish();                                    // 1.2.0: another mode ends chain entry
    leave_page_holds();
    if (page == PAGE_FUNCTION) page = page_before_function;
    else { page_before_function = page; page = PAGE_FUNCTION; fn_select(FP_CUTOFF); }
  }

  if (pressed & (1u << KEY_NOTE)) {
    // Double-tap Note: record. A tap while recording (or armed) stops it.
    uint32_t now = now_ms();
    chain_finish();                                    // 1.2.0: another mode ends chain entry
    if (page == PAGE_NOTE && rec_armed) { rec_armed = false; now = 0; }
    else if (page == PAGE_NOTE && note_tap_ms && now - note_tap_ms <= DOUBLE_TAP_MS) { rec_armed = true; now = 0; }
    leave_page_holds();
    page = PAGE_NOTE;
    note_tap_ms = now;
  }
  if (pressed & (1u << KEY_PATTERN)) {
    // 1.2.0: Pattern ends chain entry; a double-tap starts it.
    uint32_t now = now_ms();
    if (chain_entry) { chain_finish(); now = 0; }
    else if (page == PAGE_PATTERN && pattern_tap_ms && now - pattern_tap_ms <= DOUBLE_TAP_MS) {
      chain_entry = true; chain_build_n = 0; now = 0;
    }
    pattern_tap_ms = now;
    leave_page_holds(); page = PAGE_PATTERN; copy_src = -1;
    if (!chain_entry) chain_build_n = 0;
    tr_knob_start = panel_knob(); tr_base = pat()->transpose; tr_active = false;
  }
  if ((released & (1u << KEY_PATTERN)) && !chain_entry) {
    // A slot picked while holding Pattern plays on its own: it ends a chain.
    if (chain_build_n) set_chain(chain_build, 0);
    chain_build_n = 0;
  }

  if ((pressed & (1u << KEY_WAVE)) && function) wave_combo = true;   // 0.3.4: no metronome (Ableton has one)

  if (pressed & (1u << KEY_PLAY)) {
    if (function && accent) {
      audio_profile_reset();
    } else if (function) {
      uint8_t avg = audio_cpu_avg(), peak = audio_cpu_peak();
      midi_send(0x9F, avg, peak ? peak : 1);
      midi_send(0x8F, avg, 0);
      telemetry_send();
    } else {
      seq_mute_request = !seq_mute_request;           // mute / unmute (also while the host is stopped)
    }
  }

  if (page != PAGE_NOTE) rec_armed = false;          // leaving the Note page stops recording
  if (page != PAGE_FUNCTION) auto_armed = -1;        // and the Function page, knob recording
  bool recording = rec_armed && playing && page == PAGE_NOTE;

  for (int pad = 0; pad < 16; pad++) {
    uint32_t m = 1u << pad_key(pad);
    if (pressed & m) {
      if (pattern_held && glide) {                    // copy: source slot, then destination
        if (copy_src < 0) copy_src = pad;
        else { copy_pattern(copy_src, pad); copy_src = -1; }
      } else if (pattern_held && function) {          // erase that pattern
        erase_pattern(pad);
      } else if (chain_entry) {                       // 1.2.0: chain entry: add the slot
        if (chain_build_n == 0) { edit_slot = (uint8_t)pad; seq_pattern_next = edit_slot; }
        if (chain_build_n < SEQ_CHAIN_MAX) chain_build[chain_build_n++] = (uint8_t)pad;
        copy_flash_pad = pad; copy_flash_until = now_ms() + 150;   // brief flash: taken
      } else if (pattern_held) {                      // select pattern slot (the last tap wins)
        edit_slot = (uint8_t)pad;
        seq_pattern_next = edit_slot;
        tr_knob_start = panel_knob(); tr_base = pat()->transpose; tr_active = false;   // transpose this one now
        chain_build[0] = (uint8_t)pad; chain_build_n = 1;
      } else if (page == PAGE_FUNCTION && function) {  // Function + pad: clear its recording
        int a = auto_index(pad);
        if (a >= 0) {
          memset(autom[edit_slot][a], AUTO_NONE, SEQ_STEPS);
          copy_flash_pad = pad; copy_flash_until = now_ms() + 400;
        }
      } else if (page == PAGE_NOTE) {
        note_page_press(pad, recording);
      } else if (page == PAGE_PATTERN) {
        pattern_page_press(pad, accent, glide);
      } else {
        function_page_press(pad);
      }
    }
    if (released & m) {
      if (page == PAGE_NOTE || (pad <= 12 && pad_sounding[pad])) note_page_release(pad);   // a note pad held across a page change still ends
      else if (page == PAGE_PATTERN) pattern_page_release(pad);
      else function_page_release(pad);
    }
  }
}

// ---------------------------------------------------------------------------
// Knob. Held step (pitch) > Scale held (root) > Function pad held > Wave held
// (resonance) > cutoff. A tap on Wave toggles saw / square.
// ---------------------------------------------------------------------------

static void handle_knob(void) {

  int knob = panel_knob();
  uint32_t keys = merged_yummy_keys();
  bool wave_held = keys & (1u << KEY_WAVE);
  float v;

  if (wave_held && !wave_was_held) { wave_start_knob = knob; wave_turned = false; }
  if (wave_held && abs(knob - wave_start_knob) > KNOB_ACTIVATE) wave_turned = true;
  if (!wave_held && wave_was_held && wave_combo) { wave_combo = false; wave_turned = true; }
  if (!wave_held && wave_was_held && !wave_turned) {
    params.wave = params.wave == WAVE_SAW ? WAVE_SQUARE : WAVE_SAW;
    params_dirty = true;
  }
  wave_was_held = wave_held;

  if (keys & (1u << KEY_PATTERN)) {                  // Pattern + knob: transpose, relative (never jumps)
    knob_release_target();
    if (!tr_active && abs(knob - tr_knob_start) > KNOB_ACTIVATE / 2) tr_active = true;
    if (tr_active) {
      int t = transpose_from_knob(tr_base, knob - tr_knob_start);
      if (t != pat()->transpose) set_transpose(t);
      tr_turn_ms = now_ms();
    }
    return;
  }

  if (page == PAGE_PATTERN && held_step >= 0) { knob_release_target(); edit_held_step(knob); return; }

  if (page == PAGE_NOTE && scale_held) {
    knob_release_target();
    // Absolute: fully left = C (pad 1), fully right = B (pad 12); only once turned.
    if (!root_knob_active && abs(knob - scale_knob_start) > KNOB_ACTIVATE) root_knob_active = true;
    if (!root_knob_active) return;
    int r = knob * 12 / 4096;
    if (r > 11) r = 11;
    root_shown_until = now_ms() + 1000;
    if (r != root) {
      // Changing the key moves every pattern (and the selected note) with it,
      // the shorter way round; changing back restores the patterns exactly.
      int old_shift = seq_key_shift;
      root = r;
      seq_key_shift = (int8_t)seq_shift_for_root(root);
      selected_pitch += seq_key_shift - old_shift;
      for (int i = 0; i < sel_n; i++) sel_notes[i] = (uint8_t)(sel_notes[i] + seq_key_shift - old_shift);
    }
    return;
  }

  if (wave_held) {                                   // Wave + knob: volume, on every page
    if (wave_turned && knob_pickup(200, params.volume, knob, &v)) knob_set(FP_VOLUME, v);   // Wave + knob: volume
    return;
  }
  if (page == PAGE_FUNCTION) {
    if (dist_held) {
      if (abs(knob - dist_knob_start) > KNOB_ACTIVATE) dist_drive_turned = true;
      if (dist_drive_turned && knob_pickup(201, params.dist_drive, knob, &v)) {
        params.dist_drive = v;
        params_dirty = true;
        fn_turn_ms = now_ms();
      }
      return;
    }
    if (fn_pad >= 0) { knob_release_target(); function_knob_select(knob); }
    else function_knob_latched(knob);
    return;
  }



  if (knob_pickup(100 + FP_CUTOFF, params.cutoff, knob, &v)) knob_set(FP_CUTOFF, v);
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

#define L_OFF    0
#define L_DIM    3
#define L_GLOW   10
#define L_MID    30
#define L_CHORD  70
#define L_BRIGHT 140
#define L_FULL   230

// Value 0..1 as a bar across the 16 pads (the last pad partly lit).
static void bar(uint8_t *pads, float v) {
  float level = clamp01(v) * 16.0f;
  for (int i = 0; i < 16; i++) {
    if (level >= i + 1)  pads[i] = L_MID;
    else if (level > i)  pads[i] = (uint8_t)(L_DIM + (level - i) * (L_MID - L_DIM));
    else                 pads[i] = L_OFF;
  }
}

// Tune: a bar from the middle (pads 8|9) towards the value; in tune = both middle pads.
static void centre_bar(uint8_t *pads, float v) {
  float pos = clamp01(v) * 16.0f;
  for (int i = 0; i < 16; i++) pads[i] = L_OFF;
  if (v == 0.5f) { pads[7] = pads[8] = L_MID; return; }
  if (pos > 8.0f) { for (int i = 8; i < 16 && i < pos; i++) pads[i] = L_MID; }
  else            { for (int i = 7; i >= 0 && i + 1 > pos; i--) pads[i] = L_MID; }
}

// Draws a number scrolling across the pads; false once it has scrolled through.
static bool ticker_leds(uint8_t *pads, uint32_t now, int value, uint32_t start) {
  uint8_t cols[24];
  int n = ticker_columns(value, cols);
  int off = (int)((now - start) / TICK_MS);
  if (off + 4 > n) return false;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      pads[r * 4 + c] = (cols[off + c] & (8 >> r)) ? L_BRIGHT : L_OFF;
  return true;
}

static void function_page_leds(uint8_t *pads, uint32_t now) {
  bool blink = (now / 250) % 2;
  // MIDI lead: scrolls once you stop turning, when Clock is held, or after letting go of it
  bool lead_scroll = false;
  if (!dist_held && lead_tick_on && fn_pad < 0 && fn_sel == FP_LEAD && (int32_t)(now - lead_tick_start) >= 0) {
    lead_scroll = ticker_leds(pads, now, midi_lead * MIDI_LEAD_STEP_MS, lead_tick_start);
    if (!lead_scroll) lead_tick_on = false;           // scrolled through
  }
  if (dist_held && dist_drive_turned) {
    bar(pads, params.dist_drive);
    if (!knob_picked || knob_target != 201) {
      int k = panel_knob() * 16 / 4096;
      pads[k] = blink ? L_BRIGHT : L_OFF;
    }
  } else if (dist_held) {                            // Distortion held: the type on pads 1/2
    pads[0] = params.dist_type == 0 ? L_BRIGHT : L_DIM;
    pads[1] = params.dist_type == 1 ? L_BRIGHT : L_DIM;
    pads[FP_DIST] = L_FULL;
  } else if (lead_scroll) {                           // MIDI lead in ms, scrolling
  } else if (fn_pad < 0 && fn_sel == FP_CHANNEL) {   // MIDI channel: only its pad lit, while selected
    for (int i = 0; i < 16; i++) pads[i] = L_OFF;
    pads[midi_channel] = L_BRIGHT;
  } else if (fn_pad < 0 && fn_sel == FP_LEAD) {      // MIDI lead: a bar, 1 ms per pad (pad 1 = none)
    for (int i = 0; i < 16; i++) pads[i] = i < midi_lead ? L_DIM : L_OFF;
    pads[midi_lead] = L_BRIGHT;
  } else if (fn_pad < 0 && now - fn_turn_ms < 1000) { // knob being turned: the value
    if (fn_sel == FP_TUNE) centre_bar(pads, fn_get(fn_sel));
    else if (fn_sel == FP_DTIME) {                    // the division: pads 1-6, 1/16 .. 1/4
      int d = voice_delay_div(params.delay_time);
      for (int i = 0; i < VOICE_DELAY_DIVS; i++) pads[i] = i == d ? L_BRIGHT : L_DIM;
    }
    else if (fn_is_knob_param(fn_sel)) bar(pads, fn_get(fn_sel));
    else {                                            // Length
      for (int i = 0; i < pat_len(); i++) pads[i] = L_MID;
      pads[pat_len() - 1] = L_BRIGHT;
    }
    if (fn_is_knob_param(fn_sel) && (!knob_picked || knob_target != 100 + fn_sel)) {
      int span = fn_sel == FP_DTIME ? VOICE_DELAY_DIVS : 16;   // Delay Time: only pads 1-6
      int k = panel_knob() * span / 4096;  if (k > span - 1) k = span - 1;   // not picked up yet: where the knob is
      pads[k] = blink ? L_BRIGHT : L_OFF;
    }
  } else if (fn_pad < 0) {                            // overview: the selected pad, and Clock / Poly when on
    for (int i = 0; i < 16; i++) pads[i] = L_OFF;
    // 1.2.0: MIDI out and Mono/Poly: dark = off / mono (the defaults),
    // bright = on / poly.
    pads[FP_MIDIOUT] = midi_out ? L_BRIGHT : L_OFF;
    pads[FP_POLY] = pat()->poly ? L_BRIGHT : L_OFF;
    for (int a = 0; a < AUTO_PARAMS; a++)              // recorded in this pattern: faint glow
      if (auto_has(edit_slot, a)) pads[AUTO_FP[a]] = L_GLOW;
    pads[fn_sel] = L_FULL;
    if (auto_armed >= 0) pads[AUTO_FP[auto_armed]] = ((now / 100) % 2) ? L_FULL : L_OFF;   // recording: blinks fast
  } else {                                            // MIDI out / poly while held
    for (int i = 0; i < 16; i++) pads[i] = L_DIM;
    pads[FP_MIDIOUT] = midi_out ? L_BRIGHT : L_OFF;
    pads[FP_POLY]    = pat()->poly ? L_BRIGHT : L_OFF;
  }
  if (now < copy_flash_until && copy_flash_pad >= 0) pads[copy_flash_pad] = blink ? L_FULL : L_OFF;
}

// Note names on the 4x4 pads: the letter in the left 3 columns, sharps
// marked by the top-right pad. Rows top to bottom, bit 2 = left column.
static const uint8_t LETTERS[7][4] = {
  {7, 4, 4, 7},   // C
  {6, 5, 5, 6},   // D
  {7, 6, 4, 7},   // E
  {7, 6, 4, 4},   // F
  {7, 4, 5, 7},   // G
  {2, 5, 7, 5},   // A
  {6, 7, 5, 7},   // B
};
static void root_letter(uint8_t *pads, int note) {
  static const uint8_t letter[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
  static const uint8_t sharp[12]  = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
  for (int i = 0; i < 16; i++) pads[i] = L_OFF;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 3; c++)
      if (LETTERS[letter[note]][r] & (4 >> c)) pads[r * 4 + c] = L_BRIGHT;
  if (sharp[note]) pads[3] = L_BRIGHT;
}

static void update_leds(void) {
  uint32_t now = now_ms();
  uint32_t keys = merged_yummy_keys();
  bool blink = (now / 250) % 2;
  bool fast_blink = (now / 100) % 2;
  int head = seq_playhead;
  const seq_pattern_t *p = pat();
  int len = pat_len();
  bool recording = rec_armed && playing && page == PAGE_NOTE;
  uint8_t pads[16] = {0};

  if ((keys & (1u << KEY_PATTERN)) && now - tr_turn_ms < 1000) {   // transposing: the transpose
    int t = pat()->transpose;
    static const uint8_t ZERO[16] = {0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0};   // a "0" on the pads
    if (t == 0) { for (int i = 0; i < 16; i++) pads[i] = ZERO[i] ? L_BRIGHT : L_OFF; }
    else if (t > 0) pads[t] = L_BRIGHT;               // +1..+12 on pads 2-13
    else pads[t + 12] = blink ? L_BRIGHT : L_DIM;     // below 0: blinking, an octave down
    if (t != 0 && !pads[0]) pads[0] = L_GLOW;         // pad 1 faintly = where 0 is
  } else if ((keys & (1u << KEY_PATTERN)) && (keys & (1u << KEY_GLIDE))) {   // copy view
    for (int i = 0; i < 16; i++) {
      pads[i] = L_OFF;
      for (int k = 0; k < SEQ_STEPS; k++) if (seq_patterns[i].steps[k].on) { pads[i] = L_DIM; break; }
    }
    if (copy_src >= 0) pads[copy_src] = blink ? L_BRIGHT : L_OFF;   // source chosen: blinks
    if (now < copy_flash_until && copy_flash_pad >= 0) pads[copy_flash_pad] = blink ? L_FULL : L_OFF;
  } else if ((keys & (1u << KEY_PATTERN)) || chain_entry) {   // pattern select view / chain entry
    // Slots with notes dim, empty slots off, the selected slot pulses.
    uint32_t ph = now % 800;
    uint8_t pulse = (uint8_t)(2 + (ph < 400 ? ph : 800 - ph) * 38 / 400);
    // Chain (being tapped, or the current one): its slots lit.
    for (int i = 0; i < 16; i++) {
      pads[i] = L_OFF;
      for (int k = 0; k < SEQ_STEPS; k++) if (seq_patterns[i].steps[k].on) { pads[i] = L_DIM; break; }
    }
    const uint8_t *ch = chain_build_n ? chain_build : ui_chain;
    int chn = chain_build_n ? chain_build_n : ui_chain_len;
    for (int i = 0; i < chn; i++) pads[ch[i]] = L_MID;
    pads[(playing && ui_chain_len && !chain_build_n) ? seq_pattern_cur : edit_slot] = pulse;
    if (now < copy_flash_until && copy_flash_pad >= 0) pads[copy_flash_pad] = blink ? L_FULL : L_OFF;
  } else if (page == PAGE_FUNCTION) {
    function_page_leds(pads, now);
  } else if (page == PAGE_NOTE && scale_held) {
    if (now < root_shown_until) {                     // root while turning the knob: its letter
      root_letter(pads, root);
    } else {
      for (int i = 0; i < SCALE_COUNT; i++) pads[i] = (i == (int)scale) ? L_BRIGHT : L_DIM;
      pads[PAD_SCALE] = L_BRIGHT;                     // 1.2.0: lit only while held
    }
  } else if (page == PAGE_NOTE) {
    for (int i = 0; i <= 12; i++) {
      int pitch = pad_pitch(i);
      if (!seq_in_scale(pitch, scale, root)) pads[i] = L_OFF;
      else pads[i] = (((pitch - root) % 12 + 12) % 12 == 0) ? L_GLOW : L_DIM;
      if (in_notes(sel_notes, poly() ? sel_n : 1, pitch) || (!poly() && pitch == selected_pitch)) pads[i] = L_BRIGHT;
    }
    // 1.2.0: Scale dark until held; Oct -/+ dark at octave 0, lit only when
    // you are below or above it.
    pads[PAD_SCALE] = L_OFF;
    pads[PAD_OCT_DN] = entry_oct < 0 ? L_BRIGHT : L_OFF;
    pads[PAD_OCT_UP] = entry_oct > 0 ? L_BRIGHT : L_OFF;
    // 1.2.0: recording armed and playing: the playhead runs through the grid,
    // brighter on step 1, so you can see where the bar starts. Pressed and
    // selected notes stay on top.
    if (recording && head >= 0 && head < 16 && head < len && pads[head] < L_BRIGHT)
      pads[head] = head == 0 ? L_CHORD : L_MID;
  } else {                                            // pattern page
    bool acc = keys & (1u << KEY_ACCENT), gl = keys & (1u << KEY_GLIDE);
    uint32_t ph = now % 700;                          // gentle pulse, 0..20
    uint8_t pulse = (uint8_t)((ph < 350 ? ph : 700 - ph) * 20 / 350);
    for (int i = 0; i < 16; i++) {
      const seq_step_t *s = &p->steps[i];
      bool note = s->on && !s->tie, tie = s->on && s->tie;
      if (i >= len)          pads[i] = L_OFF;
      else if (acc && gl)    pads[i] = tie ? L_BRIGHT : (tie_allowed(i) ? pulse : L_OFF);
      else if (acc)          pads[i] = note ? (s->accent ? L_BRIGHT : pulse) : L_OFF;
      else if (gl)           pads[i] = s->on ? (s->slide ? L_BRIGHT : pulse) : L_OFF;
      else if (!s->on)       pads[i] = L_OFF;
      else if (tie)          pads[i] = L_GLOW;
      else                   pads[i] = (p->poly && s->chord[0]) ? L_CHORD : L_MID;   // chords a bit brighter
    }
    if (!acc && !gl) {                                // no playhead in the flag views
      if (held_step >= 0) pads[held_step] = L_BRIGHT;
      if (playing && head >= 0 && head < 16 && seq_pattern_cur == edit_slot) pads[head] = L_FULL;
    }
  }
  for (int i = 0; i < 16; i++) leds_set(i, pads[i]);

  uint8_t note_led = page == PAGE_NOTE ? L_BRIGHT : L_OFF;
  if (recording) note_led = fast_blink ? L_FULL : L_OFF;
  else if (rec_armed) note_led = blink ? L_BRIGHT : L_OFF;      // armed, waiting for the pattern to run
  leds_set(LED_NOTE, note_led);
  leds_set(LED_PATTERN, chain_entry ? (blink ? L_BRIGHT : L_OFF)   // 1.2.0: chain entry blinks
                      : (keys & (1u << KEY_PATTERN)) ? (blink ? L_BRIGHT : L_DIM)
                      : (page == PAGE_PATTERN ? L_BRIGHT : L_OFF));
  leds_set(LED_WAVE, params.wave == WAVE_SQUARE ? L_BRIGHT : L_OFF);
  uint8_t play = L_OFF;
  bool beat = head >= 0 && head % 4 == 0;
  if (playing && seq_mute_request) play = blink ? L_MID : L_OFF;               // muted
  else if (playing) play = beat ? L_FULL : L_MID;                              // pulses on the beat
  else if (seq_mute_request) play = blink ? L_DIM : L_OFF;                     // waiting, will start muted
  else play = L_DIM;                                                           // waiting for the DAW
  leds_set(LED_PLAY, play);

  if (now < flash_until) for (int i = 0; i < N_LEDS; i++) leds_set(i, L_FULL);   // audio underrun
  if (crashed && blink) for (int i = 0; i < N_LEDS; i++) leds_set(i, L_OFF);      // previous run crashed
}

// ---------------------------------------------------------------------------
// Saving: when the sequencer is stopped and nothing has changed for
// SAVE_SETTLE_MS, write everything to flash if it differs from the last save.
// ---------------------------------------------------------------------------

// Defaults first; combined journal then restores the stable merged save format.
void ui_init(void) {
  memset(autom,AUTO_NONE,sizeof(autom));
  clock_usb=1; midi_channel=1; // host clock; synth on MIDI channel 2
  seq_key_shift=(int8_t)seq_shift_for_root(root);
  seq_pattern_next=edit_slot;
}
void ui_set_active(bool active) {
  if (!active) {
    // End pad auditions only. Sequencer and incoming MIDI keep their state.
    for (int p=0;p<13;p++) if(pad_sounding[p]) note_page_release(p);
    if (held_sounding) audio_note_off();
    held_sounding=false;
    leave_page_holds();
    rec_armed=false; auto_armed=-1; rec_pads=0; group_n=0;
    chain_finish();                                   // 1.2.0: keep the slots tapped so far
    note_tap_ms=0; pattern_tap_ms=0; auto_tap_pad=-1; chain_build_n=0; copy_src=-1;
    fn_key_used=true; wave_combo=false; wave_was_held=false; wave_turned=false;
  }
  knob_release_target();
  panel_active=active;
}

// Sequencer notes from core 1 out over USB MIDI. Called from the main loop on
// every pass too, so a note doesn't wait for the next millisecond.
void ui_midi_flush(void) {
  uint8_t n, v;
  while (audio_midi_out_pop(&n, &v)) note_out(n, v);
}

void ui_task_1ms(void) {
  static uint32_t tick;
  // long press on Clock Source: MIDI out on / off
  seq_midi_enabled = midi_out;
  seq_midi_lead = (uint32_t)midi_lead * MIDI_LEAD_STEP_MS * (VOICE_FS / 1000);
  audio_metronome(false);                            // 0.3.4: no metronome; Ableton has one
  // Chain playing: the pages follow the pattern that's playing, except while
  // a key is held (so an edit never jumps to another pattern half way).
  if (playing && ui_chain_len && seq_pattern_cur != edit_slot && !merged_yummy_keys() && held_step < 0)
    edit_slot = seq_pattern_cur;
  ui_midi_flush();
  if (panel_active) { handle_keys(); if(merged_panel_ready()) handle_knob(); }
  auto_task();
  record_ties();
  if (params_dirty) { audio_set_params(&params); params_dirty = false; }
  if (++tick % 10 == 0 && panel_active) update_leds();
  // Shared router owns LED and persistence service.
}

// Compact the cold serialization path to retain RAM headroom on RP2040.
// Audio/control processing retains its existing optimization settings.
#if defined(__GNUC__) && !defined(__clang__)
#define STORE_COLD __attribute__((optimize("Os")))
#else
#define STORE_COLD
#endif
// Stream a stable-layout representation using small range copies. No full
// second pattern bank is allocated in RAM. The journal checks generation/CRC.
static void save_field(uint32_t offset,uint8_t *dst,uint32_t len,uint32_t at,const void *src,uint32_t size){
 uint32_t lo=offset>at?offset:at,hi=offset+len<at+size?offset+len:at+size;
 if(lo<hi)memcpy(dst+lo-offset,(const uint8_t *)src+lo-at,hi-lo);
}
_Static_assert(sizeof(store_data_t)==4280,"Update combined save layout for changed Yummy state");
uint32_t ui_store_size(void){return sizeof(store_data_t);}
STORE_COLD void ui_store_read(uint32_t offset,void *out,uint32_t len){
 uint8_t *dst=out;memset(dst,0,len);
 store_voice_params_t saved_params;memset(&saved_params,0,sizeof(saved_params));
 saved_params.cutoff=params.cutoff;
 saved_params.resonance=params.resonance;
 saved_params.envmod=params.envmod;
 saved_params.decay=params.decay;
 saved_params.accent=params.accent;
 saved_params.tune=params.tune;
 saved_params.volume=params.volume;
 saved_params.distortion=params.distortion;
 saved_params.wave=params.wave;
 saved_params.dist_type=params.dist_type;
 saved_params.delay_time=params.delay_time;
 saved_params.delay_amount=params.delay_amount;
 float bpm=clock_usb?stored_bpm:seq_bpm;
 uint8_t r=(uint8_t)root,sc=(uint8_t)scale,ch=midi_channel,slot=edit_slot,cl=(uint8_t)ui_chain_len;
 // Bit 1 is "MIDI out off" from 0.3.2 (it was "on"): older saves, written
 // with it clear, load with MIDI out on, the new default.
 // Bit 2 (the old metronome flag) marks the lead in whole ms (0.3.5); older
 // saves held it in 4 ms units.
 // Bit 0 (the old clock-source flag; the clock is always USB since 0.3.4) is
 // written clear from 0.3.6 on: "bit 1 holds a MIDI-out choice made with 0.3.6".
 uint8_t flags=(uint8_t)(((!midi_out)<<1)|(1u<<2)|((midi_lead&15)<<3)|0x80);
 #define FIELD(name,p,n) save_field(offset,dst,len,offsetof(store_data_t,name),p,n)
 FIELD(params,&saved_params,sizeof(saved_params));FIELD(bpm,&bpm,sizeof(bpm));
 FIELD(root,&r,1);FIELD(scale,&sc,1);FIELD(midi_channel,&ch,1);FIELD(clock_usb,&flags,1);FIELD(pattern,&slot,1);
 FIELD(patterns,seq_patterns,sizeof(seq_patterns));FIELD(chain_len,&cl,1);FIELD(chain,ui_chain,cl);
 FIELD(autom,autom,sizeof(autom));
 #undef FIELD
}
STORE_COLD bool ui_store_validate(const void *src,uint32_t len){
 if(len!=sizeof(store_data_t))return false;
 const store_data_t *d=src;
 const store_voice_params_t *p=&d->params;
 float values[]={p->cutoff,p->resonance,p->envmod,p->decay,p->accent,p->tune,p->volume,p->distortion,p->delay_time,p->delay_amount};
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)if(!isfinite(values[i])||values[i]<0||values[i]>1)return false;
 if((unsigned)p->wave>WAVE_SQUARE||p->dist_type>1||!isfinite(d->bpm)||d->bpm<20||d->bpm>300||d->root>11||d->scale>=SCALE_COUNT||d->midi_channel>15||d->pattern>15||d->chain_len>SEQ_CHAIN_MAX)return false;
 for(int i=0;i<d->chain_len;i++)if(d->chain[i]>15)return false;
 for(int i=0;i<SEQ_PATTERNS;i++){
  const seq_pattern_t *q=&d->patterns[i];
  if(q->length<1||q->length>16||q->swing>100||q->transpose< -12||q->transpose>12||q->poly>1)return false;
  for(int st=0;st<16;st++){
   const seq_step_t *n=&q->steps[st];
   if(n->pitch>127||n->on>1||n->accent>1||n->slide>1||n->tie>1)return false;
   for(int k=0;k<3;k++)if(n->chord[k]>127)return false;
  }
 }
 return true;
}
STORE_COLD bool ui_store_apply(const void *src,uint32_t len){
 if(!ui_store_validate(src,len))return false;
 const store_data_t *d=src;
 params.cutoff=d->params.cutoff;
 params.resonance=d->params.resonance;
 params.envmod=d->params.envmod;
 params.decay=d->params.decay;
 params.accent=d->params.accent;
 params.tune=d->params.tune;
 params.volume=d->params.volume;
 params.distortion=d->params.distortion;
 params.wave=d->params.wave;
 params.dist_type=d->params.dist_type;
 params.delay_time=d->params.delay_time;
 params.delay_amount=d->params.delay_amount;
 params.dist_drive=0.5f;params_dirty=true;seq_bpm=stored_bpm=d->bpm;
 root=d->root;scale=(seq_scale_t)d->scale;midi_channel=d->midi_channel;orig_valid=false;
 // Saves from before 0.3.6 (bit 0 set) take the new default: MIDI out off.
 clock_usb=1;midi_out=(d->clock_usb&1)?0:!((d->clock_usb>>1)&1);metro=0;
 midi_lead=(d->clock_usb>>3)&15;
 if(!((d->clock_usb>>2)&1)){unsigned ms=midi_lead*4u;midi_lead=(uint8_t)(ms>15?15:ms);} // 4 ms units before 0.3.5
 edit_slot=d->pattern;
 memcpy(seq_patterns,d->patterns,sizeof(seq_patterns));memcpy(autom,d->autom,sizeof(autom));
 set_chain(d->chain,d->chain_len);seq_key_shift=(int8_t)seq_shift_for_root(root);seq_pattern_next=edit_slot;
 return true;
}
bool ui_save_ready(void){return !playing&&!ext_running&&n_held==0&&!held_sounding;}
// Diagnostics: which Yummy condition holds saving back (bits 3..6 of CC 83).
uint8_t ui_save_blockers(void){return (uint8_t)((playing?1:0)|(ext_running?2:0)|(n_held?4:0)|(held_sounding?8:0));}

// Use reserved journal metadata for Drive so v0.2.x records remain readable.
uint16_t ui_drive_store_code(void){
 float d=params.dist_drive;if(!isfinite(d)||d<0)d=0;if(d>1)d=1;
 return (uint16_t)(d*65535.0f+0.5f);
}
void ui_drive_store_apply(uint16_t code){params.dist_drive=(float)code/65535.0f;params_dirty=true;}
