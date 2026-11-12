// Asynchronous RP2040/W25Q16 backend, adapted from supplied Yummy storage.c.
// Both cores execute from SRAM. USB and synth service continue while the chip
// erases/programs internally; interrupts are masked only for short SPI transfers.
#include "persist.h"
#include "merged.h"
#include "panel.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/ioqspi.h"
#include "pico/bootrom.h"
#include <string.h>
static uint8_t tx[4+SAVE_PAGE_BYTES],rx[4+SAVE_PAGE_BYTES];
static void cs(bool high) {
 hw_write_masked(&ioqspi_hw->io[1].ctrl,
  (high?IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH:IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW)<<IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
  IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}
static void command(const uint8_t *t,uint8_t *r,size_t n) {
 uint32_t irq=save_and_disable_interrupts();cs(false);size_t left=n,unread=n;
 while(left||unread) {
  uint32_t sr=ssi_hw->sr;
  if((sr&SSI_SR_TFNF_BITS)&&left&&unread-left<14){ssi_hw->dr0=*t++;left--;}
  if((sr&SSI_SR_RFNE_BITS)&&unread){*r++=(uint8_t)ssi_hw->dr0;unread--;}
 }
 cs(true);restore_interrupts(irq);
}
static void enable_write(void){uint8_t t=0x06,r;command(&t,&r,1);}
static void address(uint8_t cmd,uint32_t offset) {
 tx[0]=cmd;tx[1]=(uint8_t)(offset>>16);tx[2]=(uint8_t)(offset>>8);tx[3]=(uint8_t)offset;
}
static void check_range(uint32_t offset,uint32_t n) {
 // Guard both application/manager boundaries, even if the journal is changed.
 hard_assert(offset>=SAVE_FLASH_START&&offset+n<=SAVE_FLASH_START+SAVE_BANKS*SAVE_BANK_BYTES);
}
const uint8_t *save_hw_read(uint32_t offset) {
 check_range(offset,1);return (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE+offset);
}
void save_hw_begin(void) {
 uint8_t t[2]={0x05,0},r[2];flash_do_cmd(t,r,2);
 rom_connect_internal_flash_fn connect=(rom_connect_internal_flash_fn)rom_func_lookup(ROM_FUNC_CONNECT_INTERNAL_FLASH);
 rom_flash_exit_xip_fn exit_xip=(rom_flash_exit_xip_fn)rom_func_lookup(ROM_FUNC_FLASH_EXIT_XIP);
 uint32_t irq=save_and_disable_interrupts();connect();exit_xip();restore_interrupts(irq);
}
void save_hw_end(void){uint8_t t[2]={0x05,0},r[2];flash_do_cmd(t,r,2);}
void save_hw_erase(uint32_t offset) {
 check_range(offset,SAVE_SECTOR_BYTES);hard_assert(offset%SAVE_SECTOR_BYTES==0);
 enable_write();address(0x20,offset);command(tx,rx,4);
}
void save_hw_program(uint32_t offset,const uint8_t page[SAVE_PAGE_BYTES]) {
 check_range(offset,SAVE_PAGE_BYTES);hard_assert(offset%SAVE_PAGE_BYTES==0);
 memcpy(tx+4,page,SAVE_PAGE_BYTES);enable_write();address(0x02,offset);command(tx,rx,sizeof(tx));
}
bool save_hw_busy(void){uint8_t t[2]={0x05,0},r[2];command(t,r,2);return r[1]&1;}
uint32_t save_hw_now_ms(void){return merged_millis();}
bool save_app_ready(void){return bro_save_ready()&&ui_save_ready()&&panel_keys()==0;}
uint32_t save_blockers(void){return bro_save_blockers()|((uint32_t)ui_save_blockers()<<3)|(panel_keys()?128:0);}
