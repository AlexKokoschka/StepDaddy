#include <Arduino.h>
#include <LittleFS.h>
extern "C" {
#include "persist.h"
#include "panel.h"
}
class MergedFS:public LittleFS_Program {public:lfs_t *handle(){return &lfs;}};
static MergedFS fs;
static uint8_t banks[2][SAVE_BANK_BYTES] __attribute__((aligned(4)));
#define SM_FS_BYTES 1048576u
static bool available,fault,reported;
// Storage status for diagnostics (CC 85): 0 fine, 1 unmountable partition with a
// littlefs superblock kept untouched (saving off), 2 leftovers from other
// firmware formatted at this power-up (fine now), 3 a save file could not be
// read, 4 the filesystem could not start or format.
static uint8_t storage_state;
extern "C" uint8_t sm_storage_state(void){return storage_state;}
// Called by the patched LittleFS_Program::begin when the mount fails. Our own
// filesystem starts with a littlefs superblock in block 0 or 1; an unmountable
// one is kept (saving stays off, nothing is erased). Anything else - blank
// flash or another firmware's data - is formatted. Until 0.3.5 any nonblank
// partition was kept, which left saving off for good on a board whose flash
// still held other data.
extern "C" bool sm_lfs_keep_unmountable(const volatile uint8_t *raw,uint32_t size,uint32_t block){
 bool blank=true;
 for(uint32_t i=0;i<size;i++)if(raw[i]!=0xFF){blank=false;break;}
 if(blank)return false;
 static const char magic[8]={'l','i','t','t','l','e','f','s'};
 for(uint32_t b=0;b<2&&b*block<size;b++){
  uint32_t end=block<512?block:512;
  for(uint32_t i=0;i+8<=end;i++){
   uint32_t k=0;while(k<8&&raw[b*block+i+k]==(uint8_t)magic[k])k++;
   if(k==8){storage_state=1;return true;}
  }
 }
 storage_state=2;return false;
}
static uint32_t disk_bytes;
static const char *paths[2]={"/merged-v1-a.bin","/merged-v1-b.bin"};
static void fail(unsigned bank){
 fault=true;memset(banks[bank],0xFF,SAVE_BANK_BYTES);
 if(!reported){persist_backend_error();reported=true;}
}
extern "C" bool sm_storage_init(void){
 available=fault=reported=false;storage_state=0;
 memset(banks,0xFF,sizeof(banks));
 disk_bytes=(SAVE_PAGE_BYTES+bro_store_size()+ui_store_size()+4+SAVE_PAGE_BYTES-1)&~(SAVE_PAGE_BYTES-1u);
 if(disk_bytes>SAVE_BANK_BYTES){fault=true;return false;}
 // Build patches LittleFS_Program::begin to format only an entirely blank
 // partition. A failed mount of nonblank media disables saving, never formats it.
 // 1 MiB = 16 blocks of 64 KiB (0.3.5; was 256 KiB). littlefs keeps 2 blocks for
 // itself, each ~21 KiB journal file takes a whole block, and rewriting one
 // needs a free block: 4 blocks ran out of space at the second file (or the
 // first, with older firmware's /seq.dat and /yummy.dat present), so the Step
 // Mother never saved. tests/lfs_capacity.c replays the journal on littlefs.
 available=fs.begin(SM_FS_BYTES);
 if(!available){if(storage_state!=1)storage_state=4;fault=true;return false;}
 for(unsigned b=0;b<2;b++){
  lfs_file_t f={};int err=lfs_file_open(fs.handle(),&f,paths[b],LFS_O_RDONLY);
  if(err==LFS_ERR_NOENT)continue;
  if(err<0){fault=true;storage_state=3;continue;}
  lfs_ssize_t n=lfs_file_read(fs.handle(),&f,banks[b],SAVE_BANK_BYTES);
  if(n<0){memset(banks[b],0xFF,SAVE_BANK_BYTES);fault=true;storage_state=3;}
  if(lfs_file_close(fs.handle(),&f)<0){fault=true;storage_state=3;}
 }
 return true;
}
extern "C" bool sm_storage_available(void){return available&&!fault;}
extern "C" void sm_storage_report_error(void){if(fault&&!reported){persist_backend_error();reported=true;}}
extern "C" bool save_app_ready(void){
 return sm_storage_available()&&bro_save_ready()&&ui_save_ready()&&!panel_keys();
}
extern "C" uint32_t save_blockers(void){
 return (sm_storage_available()?0:1)|bro_save_blockers()|((uint32_t)ui_save_blockers()<<3)|(panel_keys()?128:0);
}
static bool bounds(uint32_t off,uint32_t n,unsigned &bank,uint32_t &pos){
 if(off<SAVE_FLASH_START||off-SAVE_FLASH_START>=sizeof(banks))return false;
 uint32_t rel=off-SAVE_FLASH_START;bank=rel/SAVE_BANK_BYTES;pos=rel%SAVE_BANK_BYTES;
 return n<=SAVE_BANK_BYTES-pos;
}
extern "C" const uint8_t *save_hw_read(uint32_t off){
 unsigned b;uint32_t p;if(!bounds(off,1,b,p))return banks[0];return banks[b]+p;
}
// 0.3.5: the journal's erase and page writes only fill the RAM copy of the
// bank; the header write (programmed last, at offset 0) then writes the whole
// bank file in one go and reads it back. littlefs replaces a file atomically
// on close, so a power cut leaves the previous file (an older valid save) in
// place. Writing 256-byte pages one by one, as before, made littlefs copy and
// erase a 64 KiB block per page: about 166 erases (tens of seconds) per save.
static bool commit(unsigned b){
 if(!available||fault){fail(b);return false;}
 lfs_file_t f={};bool ok=false;
 if(lfs_file_open(fs.handle(),&f,paths[b],LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC)>=0){
  ok=lfs_file_write(fs.handle(),&f,banks[b],disk_bytes)==(lfs_ssize_t)disk_bytes;
  if(lfs_file_close(fs.handle(),&f)<0)ok=false;
 }
 if(ok&&lfs_file_open(fs.handle(),&f,paths[b],LFS_O_RDONLY)>=0){
  uint8_t chunk[SAVE_PAGE_BYTES];
  for(uint32_t at=0;ok&&at<disk_bytes;at+=sizeof(chunk))
   ok=lfs_file_read(fs.handle(),&f,chunk,sizeof(chunk))==(lfs_ssize_t)sizeof(chunk)&&memcmp(chunk,banks[b]+at,sizeof(chunk))==0;
  if(lfs_file_close(fs.handle(),&f)<0)ok=false;
 }else ok=false;
 if(!ok){fail(b);return false;}
 return true;
}
extern "C" void save_hw_begin(void){}
extern "C" void save_hw_end(void){}
extern "C" void save_hw_erase(uint32_t off){
 unsigned b;uint32_t p;if(!bounds(off,SAVE_SECTOR_BYTES,b,p)||p%SAVE_SECTOR_BYTES){fail(0);return;}
 if(!available||fault){fail(b);return;}
 memset(banks[b]+p,0xFF,SAVE_SECTOR_BYTES);
}
extern "C" void save_hw_program(uint32_t off,const uint8_t *page){
 unsigned b;uint32_t p;if(!bounds(off,SAVE_PAGE_BYTES,b,p)||p%SAVE_PAGE_BYTES){fail(0);return;}
 if(!available||fault){fail(b);return;}
 memcpy(banks[b]+p,page,SAVE_PAGE_BYTES);
 if(p==0)commit(b);   // the header comes last: write the whole bank file now
}
extern "C" bool save_hw_busy(void){return false;} // operations above are synchronous
extern "C" uint32_t save_hw_now_ms(void){return millis();}
