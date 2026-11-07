#include "Arduino.h"
#include "LittleFS.h"
#include <assert.h>
#include <stdio.h>
extern "C" {
#include "persist.h"
bool sm_storage_init(void);
void sm_storage_report_error(void);
}
std::map<std::string,std::vector<uint8_t>> disk;
bool mock_mount=true,fail_next_write=false;
static uint32_t now;
static bool playing;
static uint8_t bro[16456],yummy[4280],lead=3;
uint32_t millis(){return now;}
extern "C" uint32_t panel_keys(void){return 0;}
extern "C" bool bro_save_ready(void){return !playing;}
extern "C" bool ui_save_ready(void){return !playing;}
extern "C" uint8_t ui_save_blockers(void){return playing?1:0;}
extern "C" uint8_t bro_save_blockers(void){return 0;}
extern "C" uint32_t bro_store_size(void){return sizeof(bro);}
extern "C" uint32_t ui_store_size(void){return sizeof(yummy);}
extern "C" void bro_store_read(uint32_t o,void *p,uint32_t n){memcpy(p,bro+o,n);}
extern "C" void ui_store_read(uint32_t o,void *p,uint32_t n){memcpy(p,yummy+o,n);}
extern "C" bool bro_store_validate(const void *,uint32_t n){return n==sizeof(bro);}
extern "C" bool ui_store_validate(const void *,uint32_t n){return n==sizeof(yummy);}
extern "C" bool bro_store_apply(const void *p,uint32_t n){assert(n==sizeof(bro));memcpy(bro,p,n);return true;}
extern "C" bool ui_store_apply(const void *p,uint32_t n){assert(n==sizeof(yummy));memcpy(yummy,p,n);return true;}
extern "C" uint16_t ui_drive_store_code(void){return 32768;}
extern "C" void ui_drive_store_apply(uint16_t code){(void)code;}
extern "C" uint8_t bro_lead_get(void){return lead;}
extern "C" void bro_lead_set(uint8_t n){lead=n;}
static void tick(unsigned n){while(n--){now++;persist_task();}}
static void reboot(){now=0;playing=false;assert(sm_storage_init());persist_init();sm_storage_report_error();}
extern "C" bool sm_lfs_keep_unmountable(const volatile uint8_t *,uint32_t,uint32_t);
extern "C" uint8_t sm_storage_state(void);
int main(){
 {// 0.3.5: which unmountable partitions are formatted.
  static uint8_t img[262144];const uint32_t B=65536;
  memset(img,0xFF,sizeof(img));assert(!sm_lfs_keep_unmountable(img,sizeof(img),B));     // blank: format
  img[3*B+40]=0x12;assert(!sm_lfs_keep_unmountable(img,sizeof(img),B));                  // leftovers: format
  assert(sm_storage_state()==2);
  memcpy(img+B+8,"littlefs",8);assert(sm_lfs_keep_unmountable(img,sizeof(img),B));        // our superblock: keep
  assert(sm_storage_state()==1);
  memset(img,0xFF,sizeof(img));memcpy(img+8,"littlefs",8);assert(sm_lfs_keep_unmountable(img,sizeof(img),B));
  memset(img,0xFF,sizeof(img));memcpy(img+2*B+8,"littlefs",8);assert(!sm_lfs_keep_unmountable(img,sizeof(img),B)); // another size's FS
 }

 // Existing standalone files must survive every merged journal operation.
 disk["/seq.dat"]={1,2,3,4};reboot();bro[0]=7;yummy[0]=8;tick(2600);
 assert(persist_successes()==1&&persist_errors()==0);
 for(const auto &f:disk)if(f.first!="/seq.dat")assert(f.second.size()<=21248);
 auto baseline=disk;
 // Reboot between every common journal task boundary while replacing a bank.
 for(unsigned cut=0;cut<550;cut++){
  disk=baseline;reboot();assert(persist_loaded()&&bro[0]==7&&yummy[0]==8);
  bro[0]=17;yummy[0]=18;persist_activity();tick(1500+cut);
  reboot();assert(persist_loaded());
  assert((bro[0]==7&&yummy[0]==8)||(bro[0]==17&&yummy[0]==18));
  assert(disk["/seq.dat"]==std::vector<uint8_t>({1,2,3,4}));
 }
 disk=baseline;reboot();bro[0]=20;persist_activity();playing=true;tick(3000);
 assert(disk==baseline);playing=false;fail_next_write=true;tick(2500);
 assert(persist_errors()>0);reboot();assert(persist_loaded()&&bro[0]==7);
 // Nonblank failed mount remains untouched and is exposed as a diagnostic error.
 auto unchanged=disk;mock_mount=false;assert(!sm_storage_init());assert(save_blockers()&1);persist_init();sm_storage_report_error();
 assert(persist_errors()==1);tick(3000);assert(disk==unchanged);
 puts("PASS Teensy file backend: 550 task-boundary interruptions, real journal + backend, stopped-only writes, failure recovery, standalone-file preservation and bounded files");
}
