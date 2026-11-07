#ifndef FIRMWARE_VERSION_H_
#define FIRMWARE_VERSION_H_

// The Step Mother firmware: StepBro and Yummy Gummy in one. CI reads this
// line to name the build, and the StepDaddy updater reads FIRMWARE_TAG from
// the image to show which version a file holds.
#define FIRMWARE_VERSION_STRING "1.0.0"
#define FIRMWARE_BOARD_NAME     "Step Mother"

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
