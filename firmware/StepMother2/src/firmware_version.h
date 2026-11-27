#ifndef FIRMWARE_VERSION_H_
#define FIRMWARE_VERSION_H_

// The Step Mother firmware: StepBro and Yummy Gummy in one. One version for
// every board (Step Mother USB micro, Teensy 4.1; Step Mother USB-C and Step
// Sister, both RP2040); CI reads
// this line to name the build, and the StepDaddy updater reads FIRMWARE_TAG
// from the image to show which version a file holds.
#define FIRMWARE_VERSION_STRING "1.4.0"
// The product name (USB device, audio/MIDI port names and the version tag).
// The Step Sister build sets it to "Step Sister" (CMakeLists.txt).
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
