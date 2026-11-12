#include "panel.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"

// ---------------------------------------------------------------------------
// Key matrix
// ---------------------------------------------------------------------------

#define DEBOUNCE_MS 5

static uint32_t keys_state;
static uint32_t keys_pressed_acc, keys_released_acc;
static uint8_t  integrator[N_ROWS * N_COLS];

void panel_init(void) {
  for (int r = 0; r < N_ROWS; r++) {
    gpio_init(PIN_ROW0 + r);
    gpio_set_dir(PIN_ROW0 + r, GPIO_IN);   // idle rows are high-impedance...
    // ...and must have no pull: the RP2040 enables a pull-down on every pin at
    // reset. With two keys held in one column, those pull-downs dragged the
    // column low enough to read as presses of every other key in that column.
    gpio_disable_pulls(PIN_ROW0 + r);
    gpio_put(PIN_ROW0 + r, 0);
  }
  for (int c = 0; c < N_COLS; c++) {
    gpio_init(PIN_COL0 + c);
    gpio_set_dir(PIN_COL0 + c, GPIO_IN);
    gpio_pull_up(PIN_COL0 + c);
  }

  adc_init();
  adc_gpio_init(PIN_KNOB);
  adc_select_input(0);
}

uint32_t panel_scan_raw(void) {
  uint32_t bits = 0;
  for (int r = 0; r < N_ROWS; r++) {
    gpio_set_dir(PIN_ROW0 + r, GPIO_OUT);  // drive this row low
    busy_wait_us_32(5);
    uint32_t in = gpio_get_all();
    gpio_set_dir(PIN_ROW0 + r, GPIO_IN);
    for (int c = 0; c < N_COLS; c++) {
      if (!(in & (1u << (PIN_COL0 + c)))) bits |= 1u << KEY(r, c);
    }
  }
  return bits;
}

static uint32_t knob_filtered;  // 0..4095 << 4

void panel_task(void) {
  uint32_t raw = panel_scan_raw();
  for (int k = 0; k < N_ROWS * N_COLS; k++) {
    bool down = raw & (1u << k);
    if (down && integrator[k] < DEBOUNCE_MS) integrator[k]++;
    if (!down && integrator[k] > 0) integrator[k]--;
    uint32_t m = 1u << k;
    if (integrator[k] == DEBOUNCE_MS && !(keys_state & m)) {
      keys_state |= m; keys_pressed_acc |= m;
    } else if (integrator[k] == 0 && (keys_state & m)) {
      keys_state &= ~m; keys_released_acc |= m;
    }
  }

  // Knob: average 8 readings, then a slow one-pole filter (the RP2040 ADC is noisy)
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += adc_read();
  uint32_t sample = (sum / 8) << 4;
  if (knob_filtered == 0) knob_filtered = sample;
  knob_filtered += ((int32_t)sample - (int32_t)knob_filtered) / 8;
}

uint32_t panel_keys(void)     { return keys_state; }
uint32_t panel_pressed(void)  { uint32_t v = keys_pressed_acc;  keys_pressed_acc = 0;  return v; }
uint32_t panel_released(void) { uint32_t v = keys_released_acc; keys_released_acc = 0; return v; }
uint16_t panel_knob(void)     { return (uint16_t)(knob_filtered >> 4); }

// ---------------------------------------------------------------------------
// IS31FL3731 LED driver
// ---------------------------------------------------------------------------
// LED_ADDR_MAP from the earlier RP2040 firmware, confirmed on the hardware with
// led_map_builder: logical LED -> IS31FL3731 PWM register offset (frame 1).
// Order: pads 1-16, then Note, Pattern, Bank (now Wave), Play.
static const uint8_t LED_ADDR[N_LEDS] = {
  48, 64, 80, 96, 112, 128, 0, 33, 65, 81, 97, 113, 129, 1, 17, 50, 16, 32, 49, 66
};

#define IS31_CMD           0xFD
#define IS31_PAGE_FUNC     0x0B
#define IS31_REG_CONFIG    0x00
#define IS31_REG_PICTURE   0x01
#define IS31_REG_AUDIOSYNC 0x06
#define IS31_REG_SHUTDOWN  0x0A
#define IS31_PWM_BASE      0x24
#define IS31_N_PWM         144

static bool    leds_ok;
static uint8_t led_want[N_LEDS];
static int16_t led_sent[N_LEDS];

static bool is31_write(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_write_timeout_us(i2c0, LED_I2C_ADDR, buf, 2, false, 2000) == 2;
}

// Initialization runs before USB/audio start. Use checked register writes here;
// runtime PWM updates retain the existing nonblocking FIFO path.
static bool is31_read(uint8_t reg, uint8_t *value) {
  if (i2c_write_timeout_us(i2c0, LED_I2C_ADDR, &reg, 1, true, 2000) != 1)
    return false;
  return i2c_read_timeout_us(i2c0, LED_I2C_ADDR, value, 1, false, 2000) == 1;
}

static bool leds_init_attempt(void) {
  if (!is31_write(IS31_CMD, IS31_PAGE_FUNC)) return false;
  if (!is31_write(IS31_REG_SHUTDOWN, 0x00)) return false;
  sleep_ms(10);
  if (!is31_write(IS31_REG_CONFIG, 0x00) ||
      !is31_write(IS31_REG_PICTURE, 0x00) ||
      !is31_write(IS31_REG_AUDIOSYNC, 0x00)) return false;
  if (!is31_write(IS31_CMD, 0x00)) return false;
  // Clear frame data before wake to prevent a power-on flash.
  for (int i = 0; i < 0x24; i++)
    if (!is31_write((uint8_t)i, 0)) return false;
  for (int i = 0; i < IS31_N_PWM; i++)
    if (!is31_write((uint8_t)(IS31_PWM_BASE + i), 0)) return false;

  if (!is31_write(IS31_CMD, IS31_PAGE_FUNC) ||
      !is31_write(IS31_REG_SHUTDOWN, 0x01)) return false;
  sleep_ms(10);
  if (!is31_write(IS31_CMD, 0x00)) return false;
  // IS31FL3731 application note Q17: refresh on/off AND PWM data after
  // releasing shutdown; some registers may be cleared during that transition.
  // Pads 7 and 14 use PWM offsets 0 and 1 (enable register 0, bits 0 and 1).
  for (int i = 0; i < IS31_N_PWM; i++)
    if (!is31_write((uint8_t)(IS31_PWM_BASE + i), 0)) return false;
  for (int i = 0x12; i < 0x24; i++)
    if (!is31_write((uint8_t)i, 0)) return false;
  for (int i = 0; i < 0x12; i++)
    if (!is31_write((uint8_t)i, 0xFF)) return false;
  for (int i = 0; i < 0x12; i++) {
    uint8_t value;
    if (!is31_read((uint8_t)i, &value) || value != 0xFF) return false;
  }
  return true; // frame 1 remains selected for runtime PWM writes
}

void leds_init(void) {
  i2c_init(i2c0, 400 * 1000);
  gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
  leds_ok = false;
  for (int i = 0; i < N_LEDS; i++) led_sent[i] = -1;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (leds_init_attempt()) { leds_ok = true; break; }
    sleep_ms(10);
  }
  // Force all logical PWM values through the normal update path on first tick.
}

void leds_set(int led, uint8_t brightness) {
  if (led >= 0 && led < N_LEDS) led_want[led] = brightness;
}

// Non-blocking: each LED update (register + value, then STOP) is dropped into
// the I2C hardware FIFO and sent in the background, so the main loop, which
// also feeds USB audio, never waits on the bus. The FIFO holds 8 updates.
void leds_task(void) {
  if (!leds_ok) return;
  i2c_hw_t *hw = i2c_get_hw(i2c0);
  if (hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {   // NAK etc: resend everything
    (void)hw->clr_tx_abrt;
    for (int i = 0; i < N_LEDS; i++) led_sent[i] = -1;
    return;
  }
  static int next;
  for (int n = 0; n < N_LEDS; n++) {
    if (i2c_get_write_available(i2c0) < 2) break;
    int i = (next + n) % N_LEDS;
    if (led_sent[i] != led_want[i]) {
      hw->data_cmd = (uint32_t)(IS31_PWM_BASE + LED_ADDR[i]);
      hw->data_cmd = (uint32_t)led_want[i] | I2C_IC_DATA_CMD_STOP_BITS;
      led_sent[i] = led_want[i];
    }
  }
  next = (next + 1) % N_LEDS;
}
