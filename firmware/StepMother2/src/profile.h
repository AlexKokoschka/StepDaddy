#ifndef MERGED_PROFILE_H
#define MERGED_PROFILE_H
#include <stdint.h>
// All durations are wall-clock microseconds, including timer-read overhead.
// Stage values belong to the SAME worst block, not independent stage maxima.
enum { PROFILE_EVENTS, PROFILE_PARAMS, PROFILE_SEQ, PROFILE_RENDER, PROFILE_OUTPUT, PROFILE_STAGES };
typedef struct {
 uint32_t generation, peak_us, stages[PROFILE_STAGES], peak_seconds;
 uint32_t over_budget, elapsed_seconds, lifetime_peak_us, lifetime_peak_seconds;
 uint32_t warmup_peak_us, steady_peak_us, lifetime_peak_after_warmup;
} audio_profile_t;
typedef struct {
 audio_profile_t value;
 uint64_t boot_us, session_us;
} profile_state_t;
void profile_init(profile_state_t *p, uint64_t now);
void profile_reset(profile_state_t *p, uint32_t generation, uint64_t now);
void profile_record(profile_state_t *p, const uint32_t stages[PROFILE_STAGES], uint64_t now);
uint32_t profile_percent(uint32_t us);
#endif
