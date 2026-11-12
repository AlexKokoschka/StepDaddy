#ifndef STORAGE_H_
#define STORAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "seq303.h"

// Everything that survives a power cycle. Bump STORE_VERSION when the layout
// changes; an older record is then ignored and defaults are used.
#define STORE_VERSION 5      // 2: chord notes; 3: chain; 4: distortion type + delay; 5: recorded knob movements (older records are converted)

// Recorded knob movements: per pattern, per parameter, one value per step
// (0..254, AUTO_NONE = nothing recorded). Parameters: cutoff, resonance, env
// mod, decay, accent, delay time, delay amount, distortion.
#define AUTO_PARAMS 8
#define AUTO_NONE   0xFF

// Frozen pre-Drive disk layout. Never replace this with the runtime struct.
typedef struct {
  float cutoff;
  float resonance;
  float envmod;
  float decay;
  float accent;
  float tune;        // 0.5 = centre
  float volume;
  float distortion;  // 0 = clean
  voice_wave_t wave;
  uint8_t dist_type; // 0 = tube-style, 1 = overdrive (ProCo Rat style)
  float delay_time;  // 0..1 -> 1/16, 1/8T, 1/8, 1/4T, 1/8D, 1/4 (tempo-synced)
  float delay_amount;// 0 = off .. 1 = long dub tails (echo level and feedback together)
} store_voice_params_t;
#ifdef __cplusplus
static_assert(sizeof(store_voice_params_t)==44,"stable parameter record");
#else
_Static_assert(sizeof(store_voice_params_t)==44,"stable parameter record");
#endif

typedef struct {
  store_voice_params_t params;
  float   bpm;
  uint8_t root, scale;
  uint8_t midi_channel;      // 0..15
  uint8_t clock_usb;         // 0 = internal, 1 = USB MIDI clock
  uint8_t pattern;           // selected pattern slot
  seq_pattern_t patterns[SEQ_PATTERNS];
  uint8_t chain_len;         // pattern chain (0 = none)
  uint8_t chain[SEQ_CHAIN_MAX];
  uint8_t autom[SEQ_PATTERNS][AUTO_PARAMS][SEQ_STEPS];
} store_data_t;

// Loads the newest valid record into *d. Returns false if there is none.
bool storage_load(store_data_t *d);

// Starts writing *d as a new record in the background; returns false if a
// save is still running. Call storage_task() every millisecond to carry it out.
bool storage_save(const store_data_t *d, uint32_t crc);   // crc = storage_crc(d)
void storage_task(void);
bool storage_busy(void);

uint32_t storage_crc(const void *data, uint32_t len);

#endif
