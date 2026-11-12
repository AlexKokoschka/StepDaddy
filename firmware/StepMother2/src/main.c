// Yummy Gummy: TB-303-style synth firmware for the Step Mother 2 (RP2040) PCB.
//
// Core 0 (this file): USB (MIDI + audio), panel, LEDs, bootloader combo.
// Core 1 (audio.c): sequencer + 303 voice. Panel behaviour is in ui.c.
//
//  - USB: class-compliant MIDI + 48 kHz stereo audio input, "Step Mother Yummy".
//  - Bootloader: hold physical Shift/Glide + Clear/Function at power-up only.
//  - If a run crashes, the board reboots, the LEDs blink and the crash reason
//    replaces the USB device name.

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/platform.h"
#include "hardware/structs/usb.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "firmware_version.h"
#include "panel.h"
#include "audio.h"
#include "ui.h"
#include "diag.h"
#include "version.h"
#include "merged.h"
#include "persist.h"
#include "telemetry.h"

#define FRAMES_PER_MS   (AUDIO_SAMPLE_RATE / 1000)

static volatile bool streaming;
static uint32_t sample_rate = AUDIO_SAMPLE_RATE;
static uint8_t  clock_valid = 1;
static const char *crash_report;   // set if the previous run crashed

void usb_set_product_name(const char *name);

// Bounded MIDI transmit FIFO: keep dense chords/hits across USB backpressure.
#define MIDI_TX_PACKETS 256
static uint8_t midi_tx[MIDI_TX_PACKETS][4];
static uint32_t midi_wr,midi_rd;
volatile uint32_t merged_midi_tx_dropped;
void midi_send(uint8_t status,uint8_t d1,uint8_t d2) {
  if(!tud_midi_mounted()) return;
  if(midi_wr-midi_rd>=MIDI_TX_PACKETS) {merged_midi_tx_dropped++;return;}
  uint8_t *p=midi_tx[midi_wr%MIDI_TX_PACKETS];
  p[0]=status>>4; p[1]=status;p[2]=d1&127;p[3]=d2&127;
  midi_wr++;
}
static void midi_transport_flush(void) {
  if(!tud_midi_mounted()) {midi_rd=midi_wr;return;}
  while(midi_rd!=midi_wr && tud_midi_packet_write(midi_tx[midi_rd%MIDI_TX_PACKETS])) midi_rd++;
}

static volatile uint32_t last_packet_us, late_packets;
uint32_t merged_usb_late_packets(void){return late_packets;}
uint32_t merged_midi_dropped(void){return merged_midi_tx_dropped;}

static void handle_midi_in(void) {
  uint8_t pkt[4];
  while (tud_midi_available() && tud_midi_packet_read(pkt)) {
    if (pkt[1] >= 0xF0) {
      if(pkt[1]==0xFA||pkt[1]==0xFB||pkt[1]==0xFC||pkt[1]==0xF2)persist_activity();
      bro_system(pkt[1]);
      ui_midi_system(pkt[1], pkt[2], pkt[3]);
    }   // clock, start/stop, song position
    else {persist_activity();ui_midi_in(pkt[1] & 0xF0, pkt[1] & 0x0F, pkt[2], pkt[3]);}
  }
}

// ---------------------------------------------------------------------------
// Bootloader combo
// ---------------------------------------------------------------------------

// Same physical button as always (old Shift), which is Glide since v12
#define COMBO ((1u << KEY_GLIDE) | (1u << KEY_FUNCTION))

static void check_bootloader_at_power_up(void) {
  sleep_ms(20);
  for (int i = 0; i < 10; i++) {
    if ((panel_scan_raw() & COMBO) != COMBO) return;
    sleep_ms(5);
  }
  { diag_leaving(); reset_usb_boot(0, 0); }
}

// ---------------------------------------------------------------------------
// Firmware manager
// ---------------------------------------------------------------------------

// Test image intentionally bypasses manager adoption; no existing saves are used.

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  telemetry_stack_init(0);
  crash_report = diag_init();
  if (crash_report) usb_set_product_name(crash_report);


  panel_init();
  check_bootloader_at_power_up();
  (void)*(volatile const char *)firmware_tag;   // keep the version tag in the image

  diag_start_watchdog();

  leds_init();
  audio_start_core1();
  ui_init();
  merged_init();
  persist_init();
  tud_init(0);

  uint32_t next_ms = to_ms_since_boot(get_absolute_time());
  while (true) {
    diag_feed();
    tud_task();
    handle_midi_in();
    ui_midi_flush();
    merged_task_fast();
    midi_transport_flush();

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now - next_ms) >= 0) {   // 1 kHz housekeeping
      next_ms = now + 1;
      panel_task();
      ui_status(tud_mounted(), streaming, crash_report != NULL, audio_underruns() + late_packets);
      merged_task_1ms();

    }
  }
}

// ---------------------------------------------------------------------------
// USB device callbacks
// ---------------------------------------------------------------------------

void tud_umount_cb(void)  { streaming = false; }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; streaming = false; }

// ---------------------------------------------------------------------------
// USB audio callbacks
// ---------------------------------------------------------------------------

// Host selected an alternate setting on the streaming interface
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = tu_u16_low(p_request->wIndex);
  uint8_t alt = tu_u16_low(p_request->wValue);
  if (itf == ITF_NUM_AUDIO_STREAMING) {
    if (alt != 0) audio_reset_stream();
    last_packet_us = 0;
    streaming = (alt != 0);
  }
  return true;
}

// Cancel the audio IN packet still waiting in the USB hardware.
// On the RP2040, TinyUSB never closes an isochronous endpoint, so when the host
// switches the stream off and on again (macOS does this every time an app starts
// audio), the driver arms the endpoint a second time and panics with
// "ep 81 was already available", freezing the board. Clearing the buffer here,
// whenever the streaming interface is switched, prevents that.
static void abort_audio_in_endpoint(void) {
  const uint32_t mask = USB_EP_ABORT_EP1_IN_BITS;   // EPNUM_AUDIO_IN = 0x81
  const bool can_abort = rp2040_chip_version() >= 2;
  if (can_abort) {
    hw_set_bits(&usb_hw->abort, mask);
    while ((usb_hw->abort_done & mask) != mask) tight_loop_contents();
  }
  usb_dpram->ep_buf_ctrl[EPNUM_AUDIO_IN & 0x0F].in = 0;
  if (can_abort) {
    hw_clear_bits(&usb_hw->abort_done, mask);
    hw_clear_bits(&usb_hw->abort, mask);
  }
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  if (tu_u16_low(p_request->wIndex) == ITF_NUM_AUDIO_STREAMING) {
    abort_audio_in_endpoint();
    streaming = false;
  }
  return true;
}

// Called once per USB frame (1 ms) before the next packet is loaded:
// hand TinyUSB exactly 48 stereo frames.
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
  (void)rhport; (void)func_id; (void)ep_in; (void)cur_alt_setting;
  // If a packet is still waiting in the hardware (a stale "transfer done" event
  // can arrive after the host restarted the stream), cancel it before TinyUSB
  // arms the endpoint again; arming it twice makes the RP2040 driver panic.
  if (usb_dpram->ep_buf_ctrl[EPNUM_AUDIO_IN & 0x0F].in & USB_BUF_CTRL_AVAIL) abort_audio_in_endpoint();
  // A gap of 2 ms or more means a USB frame went out empty: the main loop was
  // busy too long. Counted with the underruns (all LEDs flash).
  uint32_t now = time_us_32();
  if (last_packet_us && now - last_packet_us > 1800 && now - last_packet_us < 50000) late_packets++;
  last_packet_us = now;
  int16_t buf[FRAMES_PER_MS * AUDIO_N_CHANNELS];
  audio_read_stereo(buf, FRAMES_PER_MS);
  tud_audio_write(buf, sizeof(buf));
  return true;
}

bool tud_audio_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
  (void)rhport; (void)n_bytes_copied; (void)func_id; (void)ep_in; (void)cur_alt_setting;
  return true;
}

// Class-specific GET requests on an entity (clock source)
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t ctrl_sel  = TU_U16_HIGH(p_request->wValue);
  uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);

  if (entity_id == UAC2_ENTITY_CLOCK) {
    if (ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ) {
      if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &sample_rate, sizeof(sample_rate));
      }
      if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
        audio_control_range_4_n_t(1) range = {
          .wNumSubRanges = tu_htole16(1),
          .subrange[0] = {.bMin = tu_htole32(AUDIO_SAMPLE_RATE), .bMax = tu_htole32(AUDIO_SAMPLE_RATE), .bRes = 0},
        };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range, sizeof(range));
      }
    } else if (ctrl_sel == AUDIO_CS_CTRL_CLK_VALID && p_request->bRequest == AUDIO_CS_REQ_CUR) {
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &clock_valid, sizeof(clock_valid));
    }
  }
  return false;  // stall anything we don't support
}

// Class-specific SET requests on an entity: accept the host confirming 48 kHz
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void)rhport;
  uint8_t ctrl_sel  = TU_U16_HIGH(p_request->wValue);
  uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
  if (entity_id == UAC2_ENTITY_CLOCK && ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ &&
      p_request->bRequest == AUDIO_CS_REQ_CUR && p_request->wLength == 4) {
    uint32_t hz = tu_le32toh(*(uint32_t *)buf);
    return hz == AUDIO_SAMPLE_RATE;
  }
  return false;
}
