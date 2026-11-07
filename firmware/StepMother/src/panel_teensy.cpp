#include <Arduino.h>
#include <Wire.h>
extern "C" {
#include "panel.h"
}
static uint32_t keys,pressed,released,knob_filtered;
static uint8_t integrator[25],want[20];
static int16_t sent[20];
static const uint8_t addr[20]={48,64,80,96,112,128,0,33,65,81,97,113,129,1,17,50,16,32,49,66};
static bool leds_ok;
static bool write_reg(uint8_t r,uint8_t v){Wire.beginTransmission(LED_I2C_ADDR);Wire.write(r);Wire.write(v);return Wire.endTransmission()==0;}
static bool read_reg(uint8_t r,uint8_t &v){
 Wire.beginTransmission(LED_I2C_ADDR);Wire.write(r);
 if(Wire.endTransmission(false))return false;
 if(Wire.requestFrom((uint8_t)LED_I2C_ADDR,(uint8_t)1)!=(uint8_t)1)return false;
 v=Wire.read();return true;
}
extern "C" void panel_init(void){
 for(int r=0;r<5;r++){pinMode(PIN_ROW0+r,INPUT);digitalWrite(PIN_ROW0+r,LOW);}
 for(int c=0;c<5;c++)pinMode(PIN_COL0+c,INPUT_PULLUP);
 analogReadResolution(12);analogReadAveraging(8);pinMode(PIN_KNOB,INPUT);
}
extern "C" uint32_t panel_scan_raw(void){
 uint32_t bits=0;
 for(int r=0;r<5;r++){
  pinMode(PIN_ROW0+r,OUTPUT);digitalWriteFast(PIN_ROW0+r,LOW);delayMicroseconds(5);
  for(int c=0;c<5;c++)if(!digitalReadFast(PIN_COL0+c))bits|=1u<<KEY(r,c);
  pinMode(PIN_ROW0+r,INPUT);
 }
 return bits;
}
extern "C" void panel_task(void){
 uint32_t raw=panel_scan_raw();
 for(int k=0;k<25;k++){
  uint32_t m=1u<<k;
  if(raw&m){if(integrator[k]<5)integrator[k]++;}else if(integrator[k])integrator[k]--;
  if(integrator[k]==5 && !(keys&m)){keys|=m;pressed|=m;}
  else if(integrator[k]==0 && (keys&m)){keys&=~m;released|=m;}
 }
 uint32_t sample=(uint32_t)analogRead(PIN_KNOB)<<4;
 if(!knob_filtered)knob_filtered=sample;
 knob_filtered+=(int32_t)(sample-knob_filtered)/8;
}
extern "C" uint32_t panel_keys(void){return keys;}
extern "C" uint32_t panel_pressed(void){uint32_t p=pressed;pressed=0;return p;}
extern "C" uint32_t panel_released(void){uint32_t p=released;released=0;return p;}
extern "C" uint16_t panel_knob(void){return (uint16_t)(knob_filtered>>4);}
static bool led_attempt(void){
 if(!write_reg(0xFD,0x0B)||!write_reg(0x0A,0))return false;
 delay(10);
 if(!write_reg(0,0)||!write_reg(1,0)||!write_reg(6,0)||!write_reg(0xFD,0))return false;
 for(int i=0;i<0xB4;i++)if(!write_reg((uint8_t)i,0))return false;
 if(!write_reg(0xFD,0x0B)||!write_reg(0x0A,1))return false;
 delay(10);if(!write_reg(0xFD,0))return false;
 for(int i=0x12;i<0xB4;i++)if(!write_reg((uint8_t)i,0))return false;
 for(int i=0;i<18;i++)if(!write_reg((uint8_t)i,255))return false;
 for(int i=0;i<18;i++){uint8_t value;if(!read_reg((uint8_t)i,value)||value!=255)return false;}
 return true;
}
extern "C" void leds_init(void){
 Wire.begin();Wire.setClock(400000);for(int i=0;i<20;i++)sent[i]=-1;
 for(int n=0;n<3;n++)if(led_attempt()){leds_ok=true;break;}
}
extern "C" void leds_set(int i,uint8_t v){if(i>=0&&i<20)want[i]=v;}
extern "C" void leds_task(void){
 if(!leds_ok)return;
 static unsigned next;
 // At most two short Wire transactions per UI tick; audio IRQ remains enabled.
 unsigned writes=0;
 for(unsigned n=0;n<20&&writes<2;n++){
  unsigned i=(next+n)%20;
  if(sent[i]!=want[i]){if(write_reg((uint8_t)(0x24+addr[i]),want[i]))sent[i]=want[i];writes++;}
 }
 next=(next+1)%20;
}
