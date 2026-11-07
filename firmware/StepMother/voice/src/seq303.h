// TB-303-style step sequencer. Portable C like the voice: it runs on core 1 in
// the firmware (sample-accurate, driven by the audio blocks) and on a computer
// for testing. Core 0 edits the pattern memory directly; the sequencer reads
// a step only when that step starts.

#ifndef SEQ303_H_
#define SEQ303_H_

#include <stdint.h>
#include <stdbool.h>
#include "voice303.h"

#define SEQ_STEPS      16
#define SEQ_PATTERNS   16
#define SEQ_BASE_NOTE  36      // C2: lowest pad on the Note page at octave 0
#define SEQ_PITCH_MIN  24      // C1
#define SEQ_PITCH_MAX  72      // C5

typedef struct {
  uint8_t pitch;     // MIDI note number
  uint8_t on;        // 0 = rest (pitch is meaningless)
  uint8_t accent;
  uint8_t slide;     // slide from this step into the next
  uint8_t tie;       // hold the previous note through this step (pitch ignored)
  uint8_t chord[3];  // more notes of a chord (0 = none), stored like pitch;
                     // played only in poly patterns (mono plays `pitch`, the lowest)
} seq_step_t;


typedef struct {
  seq_step_t steps[SEQ_STEPS];
  uint8_t length;    // 1..16
  uint8_t swing;     // 0..100 -> 50% .. 75% (every second step pushed late)
  int8_t  transpose; // semitones, applied at playback
  uint8_t poly;      // 0 = mono, 1 = poly (paraphonic chords, up to 4 notes)
} seq_pattern_t;

#define SEQ_MAX_NOTES  4

// The notes a step plays, as heard (key shift and transpose applied), lowest
// first. Returns how many: 1 in a mono pattern, up to 4 in a poly one.
int seq_step_notes(const seq_pattern_t *pat, const seq_step_t *s, uint8_t *notes);
// Notes stored in a step, as heard; sets them from a chord (1..4 notes).
int  seq_step_get(const seq_pattern_t *pat, const seq_step_t *s, uint8_t *notes);
void seq_step_set(const seq_pattern_t *pat, seq_step_t *s, const uint8_t *notes, int n);

extern seq_pattern_t     seq_patterns[SEQ_PATTERNS];
extern volatile float    seq_bpm;            // set by core 0
extern volatile uint8_t  seq_pattern_next;   // set by core 0: switches at the end of the pattern
extern volatile uint8_t  seq_pattern_cur;    // owned by the sequencer

// Pattern chain: patterns played one after another (repeats allowed), each
// for one pass of its own length, then round again. Fewer than 2 entries = no
// chain (seq_pattern_next plays). Core 0 writes the list and then bumps
// seq_chain_version; a new chain starts when the current pattern ends (or
// at start). Synced, the chain lines up with the song: bar 1 = its start.
#define SEQ_CHAIN_MAX  64
extern uint8_t           seq_chain[SEQ_CHAIN_MAX];
extern volatile uint8_t  seq_chain_len;
extern volatile uint32_t seq_chain_version;
extern volatile int8_t   seq_playhead;       // current step, -1 when stopped
extern volatile uint16_t seq_step_phase;     // progress through the current step, 0..65535
extern volatile uint32_t seq_step_count;     // steps started since playback started (metronome)

// Key: step pitches are stored relative to SEQ_REF_ROOT; changing the root
// sets seq_key_shift (-6..+5 semitones, the shorter way round), so every
// pattern follows the key and changing back restores it exactly.
#define SEQ_REF_ROOT   9       // A
extern volatile int8_t   seq_key_shift;      // set by core 0
int  seq_shift_for_root(int root);

// While a live note is held (pads or MIDI), the sequencer keeps time but
// leaves the voice alone, so the live note sounds for as long as it's held.
extern bool              seq_voice_muted;    // set by the audio core

void seq_init(void);                         // empty patterns, demo in slot 1
void seq_start(voice_t *v);
void seq_stop(voice_t *v);
bool seq_playing(void);
void seq_tick(voice_t *v, int n);            // call once per audio block, before rendering it

// --- external clock (MIDI clock from a DAW) ---
// seq_start_ext() starts in synced mode; then seq_ext_step() is called on
// every 16th note with the song position in 16ths (from MIDI Song Position
// + clock ticks), so the pattern stays lined up with the DAW's bars.
// seq_bpm should follow the measured clock tempo (gate length, swing).
void seq_start_ext(voice_t *v);
// Steps are started this many samples before their tick is due, to make up for
// the audio buffer between the sequencer and the computer.
extern uint32_t seq_ext_lead;
void seq_ext_step(voice_t *v, uint32_t count);

// Board mute while synced: requested by core 0, applied at once.
extern volatile bool seq_mute_request;
extern volatile bool seq_muted;

// Sequencer notes out over MIDI: hook called from the sequencer (core 1)
// with velocity 0 for note off. Overlapping notes = slide. Chords send every note.
extern void (*seq_midi_hook)(uint8_t note, uint8_t velocity);
extern volatile bool seq_midi_enabled;
// While synced, the sequencer's MIDI notes go out this many samples before
// the board plays them (0 = with the sound), to make up for the DAW recording
// incoming MIDI late. Each step's notes are worked out ahead; when the step
// starts they're checked, and anything that changed in between (an edit, a
// mute, a pattern change) is put right then. The sound doesn't move.
extern volatile uint32_t seq_midi_lead;

// Live recording: which step a note played now belongs to. Rounds to the
// nearest step, after taking off `latency_ms` (the time between the board
// producing a sound and you hearing it through the computer).
int seq_record_step(int playhead, uint16_t phase, int length, float bpm, float latency_ms);

// --- scales (for the step-pitch knob) ---
typedef enum { SCALE_CHROMATIC, SCALE_MINOR, SCALE_MAJOR, SCALE_MINOR_PENTA,
               SCALE_PHRYGIAN, SCALE_BLUES, SCALE_COUNT } seq_scale_t;
bool seq_in_scale(int pitch, seq_scale_t scale, int root);
// Moves pitch by `degrees` notes of the scale (negative = down). A pitch that
// isn't in the scale lands on the nearest scale note in the direction moved.
int  seq_scale_move(int pitch, int degrees, seq_scale_t scale, int root);
// Nearest note in the scale; exactly in between goes up.
int  seq_scale_snap(int pitch, seq_scale_t scale, int root);

#endif
