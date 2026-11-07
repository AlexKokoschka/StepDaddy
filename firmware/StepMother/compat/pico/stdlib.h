#ifndef SM_PICO_COMPAT_H
#define SM_PICO_COMPAT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t sm_millis(void);
uint32_t sm_micros(void);
uint64_t sm_micros64(void);
#ifdef __cplusplus
}
#endif
static inline uint64_t get_absolute_time(void){return sm_micros64();}
static inline uint32_t to_ms_since_boot(uint64_t t){return (uint32_t)(t/1000);}
static inline uint32_t time_us_32(void){return sm_micros();}
static inline uint64_t time_us_64(void){return sm_micros64();}
#define __dmb() __asm volatile("dmb" ::: "memory")
#endif
