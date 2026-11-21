#pragma once
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "esp_err.h"
extern esp_err_t esp_vfs_fat_info(const char*,uint64_t*,uint64_t*);
struct statvfs {uint64_t f_bsize,f_frsize,f_blocks,f_bfree,f_bavail,f_files,f_ffree,f_favail,f_fsid,f_flag,f_namemax;};
static inline int statvfs(const char* path,struct statvfs* out){
 if(!path||!out){errno=EINVAL;return -1;}
 if(strncmp(path,"/sdcard",7)||(path[7]&&path[7]!='/')){errno=ENOTSUP;return -1;}
 uint64_t total=0,free_bytes=0;if(esp_vfs_fat_info("/sdcard",&total,&free_bytes)!=ESP_OK){errno=EIO;return -1;}
 memset(out,0,sizeof(*out));out->f_bsize=out->f_frsize=512;out->f_blocks=total/512;out->f_bfree=out->f_bavail=free_bytes/512;out->f_namemax=255;return 0;
}
