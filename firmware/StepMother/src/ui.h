#ifndef UI_H_
#define UI_H_

#include <stdint.h>
#include <stdbool.h>

// Panel behaviour: Note and Pattern pages, knob, LEDs, and MIDI notes/CCs.

void ui_init(void);                                    // also loads the saved state
void ui_task_1ms(void);                                // keys, knob, LEDs
void ui_midi_flush(void);                              // sequencer MIDI notes out (call often)
void ui_midi_in(uint8_t status, uint8_t channel, uint8_t d1, uint8_t d2);   // status without channel
void ui_midi_system(uint8_t status, uint8_t d1, uint8_t d2);              // MIDI clock / transport
void ui_status(bool usb_mounted, bool streaming, bool crashed, uint32_t underruns);

// provided by main.c
void midi_send(uint8_t status, uint8_t d1, uint8_t d2);

#endif
