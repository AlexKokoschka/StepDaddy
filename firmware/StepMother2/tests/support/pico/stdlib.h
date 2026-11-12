#ifndef HOST_PICO_H
#define HOST_PICO_H
#include <stdint.h>
extern uint64_t test_now_us;
static inline uint64_t get_absolute_time(void) {return test_now_us;}
static inline uint32_t to_ms_since_boot(uint64_t t) {return (uint32_t)(t/1000);}
static inline uint32_t time_us_32(void) {return (uint32_t)test_now_us;}
#endif
