#pragma once
#include <stdint.h>
#include <stddef.h>
#include <map>
#include <string>
#include <vector>
typedef int32_t lfs_ssize_t;
typedef int32_t lfs_soff_t;
struct lfs_t{};
struct lfs_file_t {std::string path;size_t pos=0;};
#define LFS_ERR_NOENT -2
#define LFS_O_RDONLY 1
#define LFS_O_RDWR 2
#define LFS_O_WRONLY 16
#define LFS_O_CREAT 4
#define LFS_O_TRUNC 8
#define LFS_SEEK_SET 0
extern std::map<std::string,std::vector<uint8_t>> disk;
extern bool mock_mount,fail_next_write;
class LittleFS_Program {protected:lfs_t lfs;public:bool begin(uint32_t n){return n==1048576&&mock_mount;}};
inline int lfs_file_open(lfs_t *,lfs_file_t *f,const char *p,int flags){
 if(!disk.count(p)&&!(flags&LFS_O_CREAT))return LFS_ERR_NOENT;
 f->path=p;f->pos=0;if(flags&LFS_O_TRUNC)disk[p].clear();return 0;
}
inline lfs_soff_t lfs_file_seek(lfs_t *,lfs_file_t *f,uint32_t p,int){f->pos=p;return (lfs_soff_t)p;}
inline lfs_ssize_t lfs_file_write(lfs_t *,lfs_file_t *f,const void *p,uint32_t n){
 if(fail_next_write){fail_next_write=false;return -5;}
 auto &v=disk[f->path];if(v.size()<f->pos+n)v.resize(f->pos+n,0);
 memcpy(v.data()+f->pos,p,n);f->pos+=n;return (lfs_ssize_t)n;
}
inline lfs_ssize_t lfs_file_read(lfs_t *,lfs_file_t *f,void *p,uint32_t n){
 auto &v=disk[f->path];if(f->pos>=v.size())return 0;
 size_t count=v.size()-f->pos;if(count>n)count=n;
 memcpy(p,v.data()+f->pos,count);f->pos+=count;return (lfs_ssize_t)count;
}
inline int lfs_file_sync(lfs_t *,lfs_file_t *){return 0;}
inline int lfs_file_close(lfs_t *,lfs_file_t *){return 0;}
