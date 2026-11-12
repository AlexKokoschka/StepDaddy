#ifndef PANEL_H_
#define PANEL_H_

#include <stdint.h>
#include <stdbool.h>

// Step Mother 2 (RP2040) pin map
#define PIN_ROW0        0     // GP0..GP4: matrix rows, driven low one at a time
#define PIN_COL0        5     // GP5..GP9: matrix columns, input pull-up (diode anode side)
#define PIN_I2C_SDA     16
#define PIN_I2C_SCL     17
#define PIN_KNOB        26    // ADC0
#define LED_I2C_ADDR    0x74  // IS31FL3731, AD tied to GND

#define N_ROWS 5
#define N_COLS 5

// Key index = row * 5 + col, using the brief's Appendix A positions.
#define KEY(r, c)       ((r) * N_COLS + (c))
#define KEY_NOTE        KEY(0, 0)
#define KEY_PATTERN     KEY(0, 1)
#define KEY_GLIDE       KEY(0, 2)   // was Shift (Accent and Glide swapped in v12)
#define KEY_ACCENT      KEY(0, 3)   // was Mute
#define KEY_WAVE        KEY(2, 4)   // was Bank
#define KEY_FUNCTION    KEY(3, 4)   // was Clear
#define KEY_PLAY        KEY(4, 4)

// Pads 0..15: rows 1..4, columns 0..3
static inline int pad_key(int pad) { return KEY(1 + pad / 4, pad % 4); }

void     panel_init(void);
uint32_t panel_scan_raw(void);         // one immediate scan, bit per key
void     panel_task(void);             // call every ~1 ms: debounced scan + knob
uint32_t panel_keys(void);             // debounced key state
uint32_t panel_pressed(void);          // keys newly pressed since last call
uint32_t panel_released(void);         // keys newly released since last call
uint16_t panel_knob(void);             // smoothed 0..4095

// LEDs: logical index 0..15 = pads, then these four
#define LED_NOTE     16
#define LED_PATTERN  17
#define LED_WAVE     18     // the old Bank LED
#define LED_PLAY     19
#define N_LEDS       20

void     leds_init(void);
void     leds_set(int led, uint8_t brightness);   // queued, sent by leds_task()
void     leds_task(void);                         // sends a few changed LEDs per call

#endif
