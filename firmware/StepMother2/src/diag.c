// Crash reporting for boards with no debug port.
//
// If the firmware panics, takes a hard fault or hangs, the board stores the
// reason in the watchdog scratch registers (they survive a reset) and reboots.
// On the next boot diag_init() turns that into a short message, which main.c
// puts in the USB product name so it shows up on the computer, e.g. in
// System Information > USB, and the LEDs blink.

#include <stdarg.h>
#include "diag.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#define MAGIC_PANIC  0x7B303A01u
#define MAGIC_FAULT  0x7B303A02u
#define MAGIC_RUN    0x7B303A03u   // scratch[3]: set while running, cleared before entering the bootloader

static char message[33];

// 0.3.2: a few lines of formatting instead of snprintf(), which pulled about
// 6 KB of printf code into RAM (this is a copy-to-RAM build) for these three
// messages. A panic's format string gets its one argument for the first
// conversion: %d/%i/%u in decimal, anything else in hex.
#define DIAG_COLD __attribute__((noinline, optimize("Os")))
typedef struct { char *p; char *end; } out_t;
DIAG_COLD static void put(out_t *o, char c) { if (o->p < o->end) *o->p++ = c; }
DIAG_COLD static void puts_(out_t *o, const char *s) { while (*s) put(o, *s++); }
DIAG_COLD static void putu(out_t *o, uint32_t v, unsigned base) {
  char d[10]; int n = 0;
  do { d[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v && n < 10);
  while (n) put(o, d[--n]);
}
DIAG_COLD static void format1(char *buf, size_t size, const char *prefix, const char *fmt, uint32_t arg) {
  out_t o = { buf, buf + size - 1 };
  bool used = false;
  puts_(&o, prefix);
  for (const char *f = fmt; *f; f++) {
    if (*f != '%') { put(&o, *f); continue; }
    if (f[1] == '%') { put(&o, '%'); f++; continue; }
    while (f[1] && !((f[1] >= 'a' && f[1] <= 'z') || (f[1] >= 'A' && f[1] <= 'Z')) ) f++;   // flags, width
    while (f[1] == 'l' || f[1] == 'h' || f[1] == 'z') f++;                              // length
    if (!f[1]) break;
    f++;
    if (used || *f == 's') { put(&o, '?'); continue; }                                  // no strings
    used = true;
    if (*f == 'd' || *f == 'i') { if ((int32_t)arg < 0) { put(&o, '-'); putu(&o, (uint32_t)-(int32_t)arg, 10); } else putu(&o, arg, 10); }
    else if (*f == 'u') putu(&o, arg, 10);
    else putu(&o, arg, 16);
  }
  *o.p = 0;
}

// Installed as the SDK's panic handler (PICO_PANIC_FUNCTION=tb303_panic)
void __attribute__((noreturn)) tb303_panic(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  uint32_t arg = va_arg(ap, uint32_t);
  va_end(ap);
  watchdog_hw->scratch[0] = MAGIC_PANIC;
  watchdog_hw->scratch[1] = (uint32_t)fmt;   // points into flash: still valid after reboot
  watchdog_hw->scratch[2] = arg;
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

// Replaces the SDK's default hard fault handler (which just stops)
void isr_hardfault(void) {
  watchdog_hw->scratch[0] = MAGIC_FAULT;
  watchdog_hw->scratch[1] = get_core_num();
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

const char *diag_init(void) {
  const char *result = NULL;
  uint32_t magic = watchdog_hw->scratch[0];

  if (watchdog_caused_reboot()) {
    if (magic == MAGIC_PANIC) {
      const char *fmt = (const char *)watchdog_hw->scratch[1];
      // Only trust the format string if it points into flash or RAM (with a
      // copy_to_ram build, constants live in RAM, re-copied identically at boot)
      uint32_t a = (uint32_t)fmt;
      if ((a >= 0x10000000u && a < 0x10200000u) || (a >= 0x20000000u && a < 0x20042000u)) {
        format1(message, sizeof(message), "PANIC ", fmt, watchdog_hw->scratch[2]);
      } else {
        format1(message, sizeof(message), "PANIC ", "unknown", 0);
      }
      // drop a trailing newline from the panic text
      for (char *p = message; *p; p++) if (*p == '\n' || *p == '\r') *p = 0;
      result = message;
    } else if (magic == MAGIC_FAULT) {
      format1(message, sizeof(message), "HARDFAULT core ", "%u", watchdog_hw->scratch[1]);
      result = message;
    } else if (watchdog_enable_caused_reboot() && watchdog_hw->scratch[3] == MAGIC_RUN) {
      format1(message, sizeof(message), "HANG (watchdog)", "", 0);
      result = message;
    }
  }

  watchdog_hw->scratch[0] = 0;
  return result;
}

// Call before jumping to the USB bootloader, so the reboot after flashing
// isn't mistaken for a hang.
void diag_leaving(void) {
  watchdog_hw->scratch[3] = 0;
  watchdog_hw->scratch[4] = 0;
}

void diag_start_watchdog(void) {
  watchdog_hw->scratch[3] = MAGIC_RUN;
  watchdog_enable(1000, true);   // reboot if the main loop stalls for 1 s
}

void diag_feed(void) {
  watchdog_update();
}
