#include "persist.h"
#include "merged.h"
#include "panel.h"
#include <assert.h>
#include <string.h>
static uint8_t flash[0x40000];
static bool initialized,direct;
static void init(void){if(!initialized){memset(flash,0xFF,sizeof(flash));initialized=true;}}
const uint8_t *save_hw_read(uint32_t offset){init();assert(!direct);assert(offset>=SAVE_FLASH_START&&offset<sizeof(flash));return flash+offset;}
void save_hw_begin(void){assert(!direct);direct=true;}
void save_hw_end(void){assert(direct);direct=false;}
void save_hw_erase(uint32_t offset){assert(direct&&offset>=SAVE_FLASH_START&&offset+4096<=sizeof(flash));memset(flash+offset,0xFF,4096);}
void save_hw_program(uint32_t offset,const uint8_t page[256]){assert(direct&&offset>=SAVE_FLASH_START&&offset+256<=sizeof(flash));for(int i=0;i<256;i++)flash[offset+i]&=page[i];}
bool save_hw_busy(void){assert(direct);return false;}
uint32_t save_hw_now_ms(void){return merged_millis();}
bool save_app_ready(void){return bro_save_ready()&&ui_save_ready()&&panel_keys()==0;}
uint32_t save_blockers(void){return bro_save_blockers()|((uint32_t)ui_save_blockers()<<3)|(panel_keys()?128:0);}

#include <stdio.h>
void test_import_legacy(void){
 FILE*f=fopen("tests/legacy_v023_flash.bin","rb");assert(f);
 assert(fread(flash,1,sizeof(flash),f)==sizeof(flash));assert(fclose(f)==0);initialized=true;direct=false;
}
const uint8_t *test_legacy_payload(void){
 uint32_t a,b;memcpy(&a,flash+SAVE_FLASH_START+8,4);memcpy(&b,flash+SAVE_FLASH_START+SAVE_BANK_BYTES+8,4);
 return flash+SAVE_FLASH_START+((int32_t)(b-a)>0?SAVE_BANK_BYTES:0)+SAVE_PAGE_BYTES;
}
void test_retag_metadata(uint8_t tag){
 for(unsigned k=0;k<2;k++){
  uint8_t *p=flash+SAVE_FLASH_START+k*SAVE_BANK_BYTES;
  uint32_t magic,length,bs,us;memcpy(&magic,p,4);if(magic!=PERSIST_MAGIC)continue;
  memcpy(&length,p+12,4);memcpy(&bs,p+20,4);memcpy(&us,p+24,4);
  uint8_t *extra=p+SAVE_PAGE_BYTES+bs+us;extra[1]=tag;extra[2]=extra[3]=0;
  uint32_t crc=~persist_crc(~0u,p+SAVE_PAGE_BYTES,length);memcpy(p+16,&crc,4);
  crc=~persist_crc(~0u,p,32);memcpy(p+32,&crc,4);
 }
}

void test_export_current_flash(void){FILE*f=fopen("tests/build/current_flash.bin","wb");assert(f);assert(fwrite(flash,1,sizeof(flash),f)==sizeof(flash));assert(fclose(f)==0);}
uint8_t sm_storage_state(void){return 0;}
