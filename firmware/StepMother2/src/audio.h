#ifndef AUDIO_H_
#define AUDIO_H_

#include <stdint.h>
#include <stdbool.h>
#include "voice303.h"
#include "profile.h"

// Core 1 runs the sequencer and the 303 voice into a ring buffer.
// Core 0 pulls exactly one USB frame (48 samples) per millisecond, so the
// synth runs locked to the host's USB clock and there is no drift to correct.
// Core 0 talks to core 1 only through a small event queue and a parameter
// snapshot; nothing here blocks.

void     audio_start_core1(void);

// --- control from core 0 ---
void     audio_note_on(int note, bool accent, bool legato);
void     audio_chord_on(const uint8_t *notes, int n, bool accent, voice_mode_t mode);   // live chord
void     audio_note_off(void);
void     audio_mute(bool on);         // 0.3.2: fade the synth out / back in (~5 ms)
void     audio_set_params(const voice_params_t *p);
void     audio_seq(bool run);         // start / stop the sequencer (core 1)
void     audio_metronome(bool on);    // click on every beat while the sequencer plays
void     audio_seq_ext_start(void);   // start, waiting for external clock steps
void     audio_ext_step(uint32_t count);   // external clock: song position in 16ths
bool     audio_midi_out_pop(uint8_t *note, uint8_t *vel);   // sequencer notes to send (vel 0 = off)

// --- USB side (core 0) ---
// Copies n stereo frames (L,R int16) into dst. Missing samples are filled with
// silence and counted as underruns.
void     audio_read_stereo(int16_t *dst, int n);
uint32_t audio_underruns(void);
void     audio_reset_stream(void);

// --- CPU meter: share of core 1 spent rendering, in percent ---
uint8_t  audio_cpu_avg(void);         // over the last second
uint8_t  audio_cpu_peak(void);        // worst single block in the last second

uint32_t audio_event_dropped(void);
uint32_t audio_midi_dropped(void);
uint16_t audio_cpu_peak_max(void);

void audio_profile_reset(void);
bool audio_profile_get(audio_profile_t *out);
bool audio_profile_reset_pending(void);

#endif
