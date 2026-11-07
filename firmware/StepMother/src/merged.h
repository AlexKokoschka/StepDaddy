#ifndef MERGED_H
#define MERGED_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void bro_init(void);
void bro_task(void);
void bro_key(int key,bool down);
void bro_system(uint8_t status);
void bro_suspend_panel(void);
void bro_show(void);
bool merged_bro_visible(void);
bool merged_panel_ready(void);
uint16_t merged_knob(void);
void merged_led(int led,uint8_t level);
void merged_midi_send(uint8_t status,uint8_t d1,uint8_t d2);
uint32_t merged_millis(void);
uint32_t merged_micros(void);
uint32_t merged_yummy_keys(void);
uint32_t merged_yummy_pressed(void);
uint32_t merged_yummy_released(void);
void merged_init(void);
void merged_task_1ms(void);
void merged_task_fast(void);
void ui_set_active(bool active);
// 0.3.2: Yummy mutes, toggled from StepBro's Shift+Mute (pads 2 and 3).
void ui_yummy_midi_mute_toggle(void);
void ui_yummy_audio_mute_toggle(void);
bool ui_yummy_midi_muted(void);
bool ui_yummy_audio_muted(void);
#ifdef __cplusplus
}
#endif
#endif
