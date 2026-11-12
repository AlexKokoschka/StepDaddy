#!/usr/bin/env python3
"""Compile the actual panel.c LED driver against a fault-injecting I2C chip."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=(root/'src/panel.c').read_text().split('static const uint8_t LED_ADDR')[1]
code='static const uint8_t LED_ADDR'+code
mock=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define N_LEDS 20
#define LED_I2C_ADDR 0x74
#define PIN_I2C_SDA 4
#define PIN_I2C_SCL 5
#define GPIO_FUNC_I2C 3
#define I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS 1
#define I2C_IC_DATA_CMD_STOP_BITS 512
static int *i2c0;
typedef struct {uint32_t raw_intr_stat,clr_tx_abrt,data_cmd;} i2c_hw_t;
static i2c_hw_t hw;
static uint8_t regs[12][256],page,ptr;
static int wakes,fail_writes,bad_reads,dead;
static void i2c_init(int*p,int b){(void)p;(void)b;}
static void gpio_set_function(int a,int b){(void)a;(void)b;}
static void sleep_ms(int a){(void)a;}
static int i2c_write_timeout_us(int*p,int a,const uint8_t*b,size_t n,bool nostop,int us){
 (void)p;(void)a;(void)nostop;(void)us;
 if(dead)return -1;
 if(fail_writes){fail_writes--;return -1;}
 if(n==1){ptr=b[0];return 1;}
 assert(n==2);
 if(b[0]==0xfd){page=b[1];return 2;}
 regs[page][b[0]]=b[1];
 if(page==11 && b[0]==10 && b[1]==1){
   wakes++; regs[0][0]=0; // reproduce wake clearing pads 7 and 14 enable byte
   regs[0][0x24]=0; regs[0][0x25]=0;
 }
 return 2;
}
static int i2c_read_timeout_us(int*p,int a,uint8_t*b,size_t n,bool ns,int us){
 (void)p;(void)a;(void)ns;(void)us;assert(n==1);
 if(dead)return -1;
 if(bad_reads){bad_reads--;*b=0;}else *b=regs[page][ptr];
 return 1;
}
static i2c_hw_t *i2c_get_hw(int*p){(void)p;return &hw;}
static int i2c_get_write_available(int*p){(void)p;return 16;}
'''
checks=r'''
static void check(void){
 assert(leds_ok); assert(page==0);
 for(int i=0;i<18;i++)assert(regs[0][i]==255);
 for(int i=18;i<36;i++)assert(regs[0][i]==0);
 for(int i=0;i<144;i++)assert(regs[0][36+i]==0);
 assert(LED_ADDR[6]==0 && LED_ADDR[13]==1);
 for(int i=0;i<N_LEDS;i++)assert(led_sent[i]==-1);
}
int main(void){
 leds_init();check();assert(wakes==1);
 fail_writes=1;leds_init();check();assert(wakes==2);
 bad_reads=1;leds_init();check();assert(wakes==4);
 dead=1;leds_init();assert(!leds_ok);
 puts("PASS LED initialization: wake register loss, retries, read-back, bus failure");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'led.c';p.write_text('#include <stdio.h>\n'+mock+code+checks)
 exe=Path(d)/'led';subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
