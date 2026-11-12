#include "persist.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define B 16456u
#define U 4280u
static uint8_t flash[SAVE_BANK_BYTES*2],baseline[sizeof(flash)],program_page[256];
static uint8_t bro[B],yummy[U];
static uint32_t now,offset,progress,commands;
static int pending;
static bool direct,ready=true,ignore_program,stuck;
static uint8_t lead=8;
uint32_t bro_store_size(void){return B;}
uint32_t ui_store_size(void){return U;}
void bro_store_read(uint32_t off,void *dst,uint32_t n){assert(off+n<=B);memcpy(dst,bro+off,n);}
void ui_store_read(uint32_t off,void *dst,uint32_t n){assert(off+n<=U);memcpy(dst,yummy+off,n);}
bool bro_store_validate(const void *p,uint32_t n){(void)p;return n==B;}
bool ui_store_validate(const void *p,uint32_t n){(void)p;return n==U;}
bool bro_store_apply(const void *p,uint32_t n){if(n!=B)return false;memcpy(bro,p,n);return true;}
bool ui_store_apply(const void *p,uint32_t n){if(n!=U)return false;memcpy(yummy,p,n);return true;}
uint16_t ui_drive_store_code(void){return 32768;}
void ui_drive_store_apply(uint16_t code){(void)code;}
uint8_t bro_lead_get(void){return lead;}
void bro_lead_set(uint8_t n){lead=n;}
const uint8_t *save_hw_read(uint32_t at){assert(!direct&&at>=SAVE_FLASH_START&&at<SAVE_FLASH_START+sizeof(flash));return flash+at-SAVE_FLASH_START;}
void save_hw_begin(void){assert(!direct&&!pending);direct=true;}
void save_hw_end(void){assert(direct&&!pending);direct=false;}
void save_hw_erase(uint32_t at){assert(direct&&!pending&&at%4096==0);assert(at>=SAVE_FLASH_START&&at+4096<=SAVE_FLASH_START+sizeof(flash));pending=1;offset=at-SAVE_FLASH_START;progress=0;commands++;}
void save_hw_program(uint32_t at,const uint8_t page[256]){assert(direct&&!pending&&at%256==0);assert(at>=SAVE_FLASH_START&&at+256<=SAVE_FLASH_START+sizeof(flash));pending=2;offset=at-SAVE_FLASH_START;progress=0;memcpy(program_page,page,256);commands++;}
bool save_hw_busy(void){
 assert(direct);
 if(stuck)return true;
 if(!pending)return false;
 if(pending==1){memset(flash+offset+progress,0xFF,1024);progress+=1024;if(progress==4096)pending=0;}
 else {if(!ignore_program)for(int i=0;i<64;i++)flash[offset+progress+i]&=program_page[progress+i];progress+=64;if(progress==256)pending=0;}
 return pending!=0;
}
uint32_t save_hw_now_ms(void){return now;}
bool save_app_ready(void){return ready;}
static void step(void){now++;persist_task();}
static void app(uint8_t tag){
 for(uint32_t i=0;i<B;i++)bro[i]=(uint8_t)(tag+i*7);
 for(uint32_t i=0;i<U;i++)yummy[i]=(uint8_t)(tag^((i*13)&255));
 lead=tag%17;
}
static bool is_app(uint8_t tag){
 for(uint32_t i=0;i<B;i++)if(bro[i]!=(uint8_t)(tag+i*7))return false;
 for(uint32_t i=0;i<U;i++)if(yummy[i]!=(uint8_t)(tag^((i*13)&255)))return false;
 return lead==tag%17;
}
static void reboot(void){direct=false;pending=0;ignore_program=stuck=false;ready=true;app(99);persist_init();}
static int finish(void){for(int n=0;n<4000;n++){step();if(persist_successes())return n+1;}assert(0);return 0;}
static void change(uint8_t n){app(n);persist_activity();now+=SAVE_QUIET_MS+1;}
static int power_cuts(uint8_t old,uint8_t next){
 memcpy(baseline,flash,sizeof(flash));
 reboot();assert(persist_loaded()&&is_app(old));change(next);int ticks=finish();
 for(int cut=0;cut<=ticks+2;cut++){
  memcpy(flash,baseline,sizeof(flash));reboot();assert(is_app(old));change(next);
  for(int i=0;i<cut;i++)step();
  // Power loss stops an erase/program wherever the simulated flash reached.
  reboot();assert(persist_loaded());assert(is_app(old)||is_app(next));
 }
 memcpy(flash,baseline,sizeof(flash));reboot();change(next);finish();return ticks+3;
}
int main(void){
 assert((~persist_crc(~0u,"123456789",9))==0xCBF43926u);
 memset(flash,0xFF,sizeof(flash));app(1);persist_init();now+=SAVE_QUIET_MS+1;finish();assert(persist_errors()==0);
 int cuts=power_cuts(1,2);cuts+=power_cuts(2,3);
 reboot();assert(is_app(3));
 // Corrupt the newest committed payload; boot must recover the previous copy.
 uint32_t seq=persist_sequence();int bank=seq%2?0:1;
 flash[bank*SAVE_BANK_BYTES+SAVE_PAGE_BYTES+18]^=1;
 reboot();assert(is_app(2));
 // Save after recovery, then keep a baseline for controlled failures.
 change(4);finish();memcpy(baseline,flash,sizeof(flash));
 // A failed program is detected by read-back; the previous bank remains valid.
 reboot();change(5);ignore_program=true;
 for(int i=0;i<2000&&persist_errors()==0;i++)step();
 assert(persist_errors()>0&&persist_successes()==0);reboot();assert(is_app(4));
 // Resuming transport aborts; no new flash commands may start while playing.
 memcpy(flash,baseline,sizeof(flash));reboot();change(6);
 while(persist_status()!=5)step(); // WRITE_WAIT
 ready=false;persist_activity();uint32_t count=commands;
 for(int i=0;i<2000;i++){step();}
 assert(commands==count&&!direct);
 reboot();assert(is_app(4));
 // Even a missed activity hook cannot commit a mixed streamed payload.
 memcpy(flash,baseline,sizeof(flash));reboot();change(7);
 while(persist_status()!=4)step(); // WRITE
 bro[0]^=1;step();bro[0]^=1;
 for(int i=0;i<1500&&persist_aborts()==0;i++){step();}
 assert(persist_aborts()>0);
 reboot();assert(is_app(4));
 // Erase timeout leaves the active bank untouched and never restores XIP busy.
 memcpy(flash,baseline,sizeof(flash));reboot();change(8);
 while(persist_status()!=3){step();}
 stuck=true;
 for(int i=0;i<1600;i++){step();}
 assert(persist_errors()==1&&direct);
 stuck=false;ready=false;for(int i=0;i<10;i++)step();assert(!direct);
 reboot();assert(is_app(4));
 printf("PASS journal: %d power-cut boundaries, both bank directions, CRC fallback, program failure, resume abort, mixed-payload rejection and timeout.\n",cuts);
 return 0;
}
