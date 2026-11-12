#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// ---- Common -----------------------------------------------------------------
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif
#define CFG_TUSB_DEBUG              0
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE      64

// ---- Classes ----------------------------------------------------------------
#define CFG_TUD_AUDIO               1
#define CFG_TUD_MIDI                1
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_VENDOR              0

// ---- MIDI -------------------------------------------------------------------
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     256

// ---- Audio: UAC2, device -> host, 48 kHz, 16-bit, stereo -------------------
#define AUDIO_SAMPLE_RATE           48000
#define AUDIO_N_CHANNELS            2
#define AUDIO_BYTES_PER_SAMPLE      2

// Length of our own audio function descriptor (defined in usb_descriptors.h)
#include "usb_descriptors.h"
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN               TB303_AUDIO_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT               1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ            64

#define CFG_TUD_AUDIO_ENABLE_EP_IN                  1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  AUDIO_BYTES_PER_SAMPLE
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          AUDIO_N_CHANNELS
#define CFG_TUD_AUDIO_EP_SZ_IN                      TUD_AUDIO_EP_SIZE(AUDIO_SAMPLE_RATE, AUDIO_BYTES_PER_SAMPLE, AUDIO_N_CHANNELS)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX           CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ        (4 * CFG_TUD_AUDIO_EP_SZ_IN)

// We write exactly one 1 ms packet (48 frames) per USB frame ourselves, which
// locks the synth to the host's clock; TinyUSB's own flow control isn't needed.
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL            0

#endif
