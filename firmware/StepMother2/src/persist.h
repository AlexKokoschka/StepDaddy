#ifndef MERGED_PERSIST_H
#define MERGED_PERSIST_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SAVE_FLASH_START 0x30000u
#define SAVE_BANK_BYTES 32768u
#define SAVE_BANKS 2u
#define SAVE_PAGE_BYTES 256u
#define SAVE_SECTOR_BYTES 4096u
#define SAVE_QUIET_MS 1500u
#define PERSIST_MAGIC 0x32564D53u
#define SAVE_LAYOUT 1u
#define SAVE_COMMIT 0xC04D17EDu
#ifdef __cplusplus
extern "C" {
#endif
// Live state is streamed one page at a time; no second 21 KiB snapshot.
uint32_t bro_store_size(void);
void bro_store_read(uint32_t offset,void *dst,uint32_t len);
bool bro_store_validate(const void *src,uint32_t len);
bool bro_store_apply(const void *src,uint32_t len);
bool bro_save_ready(void);
uint8_t bro_lead_get(void);
void bro_lead_set(uint8_t value);
uint16_t ui_drive_store_code(void);
void ui_drive_store_apply(uint16_t code);
uint32_t ui_store_size(void);
void ui_store_read(uint32_t offset,void *dst,uint32_t len);
bool ui_store_validate(const void *src,uint32_t len);
bool ui_store_apply(const void *src,uint32_t len);
bool ui_save_ready(void);
// Called only by core 0. In-flight payloads are abandoned on activity/resume.
void persist_init(void);
void persist_activity(void);
void persist_task(void);
bool persist_busy(void);
uint32_t persist_successes(void);
uint32_t persist_errors(void);
uint32_t persist_aborts(void);
uint32_t persist_sequence(void);
uint16_t persist_status(void);
bool persist_loaded(void);
uint32_t persist_crc(uint32_t crc,const void *src,size_t n);
// Platform backend. Access only this dedicated 64 KiB region.
const uint8_t *save_hw_read(uint32_t flash_offset);
void save_hw_begin(void);
void save_hw_end(void);
void save_hw_erase(uint32_t flash_offset);
void save_hw_program(uint32_t flash_offset,const uint8_t page[SAVE_PAGE_BYTES]);
bool save_hw_busy(void);
uint32_t save_hw_now_ms(void);
bool save_app_ready(void);
// Diagnostics (CC 83): why saving waits. 1 storage unavailable, 2 DAW playing
// (StepBro), 4 StepBro note held, 8 Yummy playing, 16 Yummy clock running,
// 32 Yummy notes held, 64 Yummy step audition, 128 a key is down.
uint32_t save_blockers(void);
uint8_t bro_save_blockers(void);
uint8_t ui_save_blockers(void);
uint32_t persist_quiet_ms(void);
#ifdef __cplusplus
}
#endif
#endif
