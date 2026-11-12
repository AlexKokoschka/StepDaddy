#include <string.h>
#include "pico/unique_id.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "firmware_version.h"

// The board shows up as "Step Mother" whichever editing level is in use (the
// device, its MIDI port and its audio interface). The version is in the
// image's tag (firmware_version.h) and in bcdDevice, not in the name, so a
// computer that remembers the device by name doesn't see a new one each update.
const char firmware_tag[] = FIRMWARE_TAG;

// Placeholder VID/PID (TinyUSB's development VID). Replace with the Raspberry Pi
// product ID (VID 0x2E8A) once allocated, before any release.
#define USB_VID   0xCAFE
#define USB_PID   0x43F1

static tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  // IAD is used for the audio function
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0300,   // firmware 0.3.0 (BCD)
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TB303_AUDIO_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  TB303_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, AUDIO_BYTES_PER_SAMPLE, AUDIO_BYTES_PER_SAMPLE * 8,
                         EPNUM_AUDIO_IN, CFG_TUD_AUDIO_EP_SZ_IN),

  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "config descriptor length mismatch");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static char const *string_desc_arr[] = {
  (const char[]){0x09, 0x04},  // 0: English
  FIRMWARE_BOARD_NAME,        // 1: Manufacturer
  FIRMWARE_BOARD_NAME,        // 2: Product
  NULL,                        // 3: Serial (board unique ID)
  FIRMWARE_BOARD_NAME,        // 4: Audio interface (the audio device name)
  FIRMWARE_BOARD_NAME,        // 5: MIDI interface (the MIDI port name)
};

static uint16_t desc_str[32 + 1];

// Lets main.c replace the product name, e.g. with a crash report
void usb_set_product_name(const char *name) {
  string_desc_arr[2] = name;   // product (System Information > USB)
  string_desc_arr[4] = name;   // audio device name (Ableton, QuickTime)
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t chr_count;

  if (index == 0) {
    memcpy(&desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else if (index == 3) {
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(serial, sizeof(serial));
    chr_count = strlen(serial);
    for (size_t i = 0; i < chr_count; i++) desc_str[1 + i] = serial[i];
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    const char *str = string_desc_arr[index];
    chr_count = strlen(str);
    if (chr_count > 32) chr_count = 32;
    for (size_t i = 0; i < chr_count; i++) desc_str[1 + i] = str[i];
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return desc_str;
}
