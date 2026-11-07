#include "persist.h"
#include <string.h>
// Header page is programmed LAST; its payload was already read-back verified.
// A torn erase/program never destroys the active bank or commits partial data.
typedef struct {
 uint32_t magic,layout,sequence,length,crc,bro_size,ui_size,extra_size;
 uint32_t header_crc;
} header_t;
_Static_assert(sizeof(header_t)==36,"fixed save header layout");
_Static_assert(SAVE_FLASH_START+SAVE_BANKS*SAVE_BANK_BYTES==0x40000u,"must end before manager");
enum {IDLE,SCAN,ERASE,ERASE_WAIT,WRITE,WRITE_WAIT,VERIFY,COMMIT,COMMIT_WAIT,ABORT_WAIT};
static uint8_t phase,page[SAVE_PAGE_BYTES];
static bool direct,loaded,have_crc,fault_seen;
static int active=-1,target;
static uint32_t length,sequence,saved_crc,expected_crc,running_crc,cursor,sector;
static uint32_t revision,start_revision,last_activity,next_scan,op_started;
static uint32_t successes,errors,aborts;
static uint32_t target_base(void) {return SAVE_FLASH_START+(uint32_t)target*SAVE_BANK_BYTES;}
uint32_t persist_crc(uint32_t crc,const void *src,size_t n) {
 const uint8_t *p=src;
 while(n--) {crc^=*p++;for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1u)));}
 return crc;
}
static uint32_t complete_crc(const void *p,size_t n) {return ~persist_crc(~0u,p,n);}
static void payload(uint32_t offset,uint8_t *dst,uint32_t n) {
 memset(dst,0,n);
 uint32_t b=bro_store_size(),u=ui_store_size();
 while(n) {
  uint32_t k;
  if(offset<b) {k=b-offset;if(k>n)k=n;bro_store_read(offset,dst,k);}
  else if(offset<b+u) {k=b+u-offset;if(k>n)k=n;ui_store_read(offset-b,dst,k);}
  else {
   uint16_t drive=ui_drive_store_code();
   uint8_t extra[4]={bro_lead_get(),0xD3,(uint8_t)drive,(uint8_t)(drive>>8)};
   uint32_t e=offset-b-u;k=4-e;if(k>n)k=n;memcpy(dst,extra+e,k);
  }
  offset+=k;dst+=k;n-=k;
 }
}
static bool bank_valid(int bank,header_t *out) {
 const uint8_t *p=save_hw_read(SAVE_FLASH_START+(uint32_t)bank*SAVE_BANK_BYTES);
 header_t h;memcpy(&h,p,sizeof(h));
 uint32_t inv,commit;memcpy(&inv,p+248,4);memcpy(&commit,p+252,4);
 if(h.magic!=PERSIST_MAGIC||h.layout!=SAVE_LAYOUT||h.length!=length||
    h.bro_size!=bro_store_size()||h.ui_size!=ui_store_size()||h.extra_size!=4||
    h.length>SAVE_BANK_BYTES-SAVE_PAGE_BYTES||inv!=~h.sequence||commit!=SAVE_COMMIT||
    h.header_crc!=complete_crc(&h,32))return false;
 if(h.crc!=complete_crc(p+SAVE_PAGE_BYTES,h.length))return false;
 *out=h;return true;
}
static bool apply_bank(int bank) {
 const uint8_t *p=save_hw_read(SAVE_FLASH_START+(uint32_t)bank*SAVE_BANK_BYTES+SAVE_PAGE_BYTES);
 uint32_t b=bro_store_size(),u=ui_store_size();
 if(!bro_store_validate(p,b)||!ui_store_validate(p+b,u)||p[b+u]>16)return false;
 if(p[b+u+1]!=0xD3 && (p[b+u+1]||p[b+u+2]||p[b+u+3]))return false;
 if(!bro_store_apply(p,b)||!ui_store_apply(p+b,u))return false;
 // Legacy reserved bytes were all zero. Unknown extensions are rejected.
 bro_lead_set(p[b+u]);
 if(p[b+u+1]==0xD3)ui_drive_store_apply((uint16_t)(p[b+u+2]|((uint16_t)p[b+u+3]<<8)));
 return true;
}

void persist_init(void) {
 phase=IDLE;direct=false;loaded=false;have_crc=false;active=-1;sequence=0;
 revision=0;successes=errors=aborts=0;fault_seen=false;
 last_activity=save_hw_now_ms();next_scan=last_activity;
 length=bro_store_size()+ui_store_size()+4;
 if(length>SAVE_BANK_BYTES-SAVE_PAGE_BYTES){errors++;return;}
 header_t a,b;bool av=bank_valid(0,&a),bv=bank_valid(1,&b);
 int first=av&&bv?((int32_t)(b.sequence-a.sequence)>0?1:0):(av?0:bv?1:-1);
 if(first>=0) {
  if(apply_bank(first)) {active=first;sequence=first?b.sequence:a.sequence;saved_crc=first?b.crc:a.crc;loaded=have_crc=true;}
  else if(av&&bv&&apply_bank(1-first)) {active=1-first;sequence=active?b.sequence:a.sequence;saved_crc=active?b.crc:a.crc;loaded=have_crc=true;errors++;}
  else errors++;
 }
}
void persist_activity(void) {revision++;last_activity=save_hw_now_ms();}
uint32_t persist_quiet_ms(void){return save_hw_now_ms()-last_activity;}
bool persist_busy(void) {return phase>=ERASE;}
uint32_t persist_successes(void){return successes;}
uint32_t persist_errors(void){return errors;}
void persist_backend_error(void){errors++;}
uint32_t persist_aborts(void){return aborts;}
uint32_t persist_sequence(void){return sequence;}
bool persist_loaded(void){return loaded;}
uint16_t persist_status(void){return phase;}
static bool eligible(void){return save_app_ready()&&revision==start_revision;}
static void close_direct(void){if(direct){save_hw_end();direct=false;}}
static void idle_after_abort(void) {
 close_direct();phase=IDLE;aborts++;next_scan=save_hw_now_ms()+SAVE_QUIET_MS;
}
static void fault(void) {if(!fault_seen){errors++;fault_seen=true;}}
static void start_op(void) {op_started=save_hw_now_ms();fault_seen=false;}
void persist_task(void) {
 uint32_t now=save_hw_now_ms();
 if(phase==IDLE) {
  if(!save_app_ready()||now-last_activity<SAVE_QUIET_MS||(int32_t)(now-next_scan)<0)return;
  start_revision=revision;cursor=0;running_crc=~0u;phase=SCAN;return;
 }
 // Once the final header program is accepted, finish it as a valid snapshot;
 // later edits will be detected and saved in the next transaction.
 if(phase!=COMMIT_WAIT&&phase!=ABORT_WAIT&&!eligible()) {
  if(phase==ERASE_WAIT||phase==WRITE_WAIT){phase=ABORT_WAIT;return;}
  idle_after_abort();return;
 }
 uint32_t n=length-cursor;if(n>SAVE_PAGE_BYTES)n=SAVE_PAGE_BYTES;
 switch(phase) {
 case SCAN:
  payload(cursor,page,n);running_crc=persist_crc(running_crc,page,n);cursor+=n;
  if(cursor==length) {
   expected_crc=~running_crc;
   if(have_crc&&expected_crc==saved_crc){phase=IDLE;next_scan=now+1000;return;}
   target=active==0?1:0;sector=0;cursor=0;running_crc=~0u;
   save_hw_begin();direct=true;phase=ERASE;
  }return;
 case ERASE:
  save_hw_erase(target_base()+sector*SAVE_SECTOR_BYTES);start_op();phase=ERASE_WAIT;return;
 case ERASE_WAIT:
  if(save_hw_busy()){if(now-op_started>1500){fault();phase=ABORT_WAIT;}return;}
  if(++sector<SAVE_BANK_BYTES/SAVE_SECTOR_BYTES)phase=ERASE;else phase=WRITE;
  return;
 case WRITE:
  memset(page,0xFF,sizeof(page));payload(cursor,page,n);
  running_crc=persist_crc(running_crc,page,n);
  save_hw_program(target_base()+SAVE_PAGE_BYTES+cursor,page);start_op();phase=WRITE_WAIT;return;
 case WRITE_WAIT:
  if(save_hw_busy()){if(now-op_started>50){fault();phase=ABORT_WAIT;}return;}
  cursor+=n;
  if(cursor<length){phase=WRITE;return;}
  close_direct();
  if(~running_crc!=expected_crc){idle_after_abort();return;}
  cursor=0;running_crc=~0u;phase=VERIFY;return;
 case VERIFY:
  memcpy(page,save_hw_read(target_base()+SAVE_PAGE_BYTES+cursor),n);
  running_crc=persist_crc(running_crc,page,n);cursor+=n;
  if(cursor==length) {
   if(~running_crc!=expected_crc){fault();idle_after_abort();return;}
   phase=COMMIT;
  }return;
 case COMMIT: {
  header_t h={PERSIST_MAGIC,SAVE_LAYOUT,sequence+1,length,expected_crc,bro_store_size(),ui_store_size(),4,0};
  h.header_crc=complete_crc(&h,32);memset(page,0xFF,sizeof(page));memcpy(page,&h,sizeof(h));
  uint32_t inv=~h.sequence,tag=SAVE_COMMIT;memcpy(page+248,&inv,4);memcpy(page+252,&tag,4);
  save_hw_begin();direct=true;save_hw_program(target_base(),page);start_op();phase=COMMIT_WAIT;return;
 }
 case COMMIT_WAIT:
  if(save_hw_busy()){if(now-op_started>50)fault();return;}
  close_direct();
  // Header read-back is small; payload was already verified page by page.
  if(memcmp(save_hw_read(target_base()),page,SAVE_PAGE_BYTES)!=0){fault();idle_after_abort();return;}
  active=target;sequence++;saved_crc=expected_crc;have_crc=true;successes++;
  phase=IDLE;next_scan=now+1000;return;
 case ABORT_WAIT:
  if(save_hw_busy())return;
  idle_after_abort();return;
 default:return;
 }
}
