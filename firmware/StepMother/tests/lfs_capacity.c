/* Replays the Step Mother save journal (storage_teensy.cpp: page writes,
   sync, read-back; header last; banks A and B) on the vendored littlefs with
   the LittleFS_Program settings, at the old 256 KiB size (must run out of
   space, the 0.3.4 bug) and the 1 MiB size (must keep working, also with
   older firmware's files present). */
#include "lfs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define BS 65536
static uint32_t NB; static uint8_t *mem;
static int rd(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,void*buf,lfs_size_t n){memcpy(buf,mem+b*BS+o,n);return 0;}
static int pr(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,const void*buf,lfs_size_t n){memcpy(mem+b*BS+o,buf,n);return 0;}
static int er(const struct lfs_config*c,lfs_block_t b){memset(mem+b*BS,0xFF,BS);return 0;}
static int sy(const struct lfs_config*c){return 0;}
static lfs_t lfs;static struct lfs_config cfg;
static int store(const char*path,uint32_t p,const uint8_t*d,uint32_t n,int trunc){
 lfs_file_t f;memset(&f,0,sizeof f);int e=lfs_file_open(&lfs,&f,path,LFS_O_RDWR|LFS_O_CREAT|(trunc?LFS_O_TRUNC:0));
 if(e<0)return e;
 uint8_t back[256];int ok=0;
 if(lfs_file_seek(&lfs,&f,p,LFS_SEEK_SET)!=(lfs_soff_t)p){ok=-100;}
 else if((e=lfs_file_write(&lfs,&f,d,n))!=(int)n){ok=e;}
 else if((e=lfs_file_sync(&lfs,&f))!=0){ok=e;}
 else if(lfs_file_seek(&lfs,&f,p,LFS_SEEK_SET)!=(lfs_soff_t)p){ok=-101;}
 else if(lfs_file_read(&lfs,&f,back,n)!=(int)n){ok=-102;}
 int c=lfs_file_close(&lfs,&f);if(!ok&&c<0)ok=c;return ok;
}
static void putfile(const char*p,uint32_t n){lfs_file_t f;lfs_file_open(&lfs,&f,p,LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC);uint8_t *b=malloc(n);memset(b,0x5A,n);lfs_file_write(&lfs,&f,b,n);lfs_file_close(&lfs,&f);free(b);}
static int journal(int bank,int rounds){
 const char*path=bank?"/merged-v1-b.bin":"/merged-v1-a.bin";
 uint32_t disk=21248;uint8_t pg[256];
 for(int r=0;r<rounds;r++){
  // erase: 8 sectors of 4 KiB but file-backed only up to disk bytes
  memset(pg,0xFF,256);
  for(uint32_t o=0;o<disk;o+=256){int e=store(path,o,pg,256,o==0);if(e){return e;}}
  memset(pg,r+1,256);
  for(uint32_t o=256;o<disk;o+=256){int e=store(path,o,pg,256,0);if(e){return e;}}
  int e=store(path,0,pg,256,0);if(e){return e;}
 }
 return 0;
}
static int run(uint32_t nb,int old){
 NB=nb;mem=malloc(NB*BS);memset(mem,0xFF,NB*BS);memset(&lfs,0,sizeof lfs);
 assert(lfs_format(&lfs,&cfg)==0&&lfs_mount(&lfs,&cfg)==0);
 if(old){putfile("/seq.dat",16400);putfile("/yummy.dat",4300);putfile("/lead.dat",2);}
 int e=journal(0,1);if(!e)e=journal(1,1);if(!e)e=journal(0,3);if(!e)e=journal(1,3);
 lfs_unmount(&lfs);free(mem);return e;
}
int main(void){
 cfg.read=rd;cfg.prog=pr;cfg.erase=er;cfg.sync=sy;cfg.read_size=128;cfg.prog_size=128;cfg.block_size=BS;cfg.block_cycles=800;cfg.cache_size=128;cfg.lookahead_size=128;cfg.name_max=LFS_NAME_MAX;
 cfg.block_count=4;assert(run(4,0)==LFS_ERR_NOSPC&&run(4,1)==LFS_ERR_NOSPC);
 cfg.block_count=16;assert(run(16,0)==0&&run(16,1)==0);
 printf("PASS littlefs capacity: 256 KiB runs out of space (0.3.4 bug), 1 MiB keeps both journal banks, also with old files\n");
 return 0;
}
