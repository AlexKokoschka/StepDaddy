#ifndef MERGED_TELEMETRY_H
#define MERGED_TELEMETRY_H
#include <stdint.h>
void telemetry_stack_init(unsigned core);
uint32_t telemetry_stack_free(unsigned core);
uint32_t telemetry_static_headroom(void);
void telemetry_send(void);
#endif
