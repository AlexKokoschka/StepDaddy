#ifndef FIRMWARE_VERSION_H_
#define FIRMWARE_VERSION_H_

// The Step Mother firmware: StepBro and Yummy Gummy in one. One version for
// both boards (Step Mother 1, Teensy 4.1, and Step Mother 2, RP2040); CI reads
// this line to name the build, and the StepDaddy updater reads FIRMWARE_TAG
// from the image to show which version a file holds.
#define FIRMWARE_VERSION_STRING "1.2.0"
// The product name (USB device, audio/MIDI port names and the version tag).
#ifndef FIRMWARE_BOARD_NAME
#define FIRMWARE_BOARD_NAME     "Step Mother"
#endif

// "SSFW|<board>|<version>|": the same tag scheme as the earlier firmwares.
#define FIRMWARE_TAG "SSFW|" FIRMWARE_BOARD_NAME "|" FIRMWARE_VERSION_STRING "|"

#ifdef __cplusplus
extern "C" {
#endif
extern const char firmware_tag[];
#ifdef __cplusplus
}
#endif

#endif
