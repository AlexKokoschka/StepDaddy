#ifndef BRO_PORT_H
#define BRO_PORT_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "merged.h"
#include "firmware_version.h"
#define FIRMWARE_VERSION FIRMWARE_VERSION_STRING
#define BOARD_NAME FIRMWARE_BOARD_NAME
#define MATRIX_SETTLE_US 20
#define SERIAL_BAUD 115200
inline uint32_t millis() {return merged_millis();}
inline uint32_t micros() {return merged_micros();}
inline long map(long v,long a,long b,long c,long d) {return (v-a)*(d-c)/(b-a)+c;}
template<class T> T constrain(T v,T lo,T hi) {return v<lo?lo:v>hi?hi:v;}
struct NullSerial {
 template<class T> void print(T) {}
 template<class T> void println(T) {}
 void println() {}
};
static NullSerial Serial;
inline void boardSendNoteOn(uint8_t n,uint8_t v,uint8_t ch) {merged_midi_send(0x90|((ch-1)&15),n,v);}
inline void boardSendNoteOff(uint8_t n,uint8_t v,uint8_t ch) {merged_midi_send(0x80|((ch-1)&15),n,v);}
#endif
