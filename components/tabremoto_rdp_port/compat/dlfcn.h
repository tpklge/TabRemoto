/* Static embedded build: dynamic loading is explicitly unsupported. */
#pragma once
#include <stddef.h>
#include <errno.h>
#define RTLD_LOCAL 0
#define RTLD_LAZY 0
#define RTLD_NOLOAD 0
static inline void* dlopen(const char* path,int flags){(void)path;(void)flags;errno=ENOSYS;return NULL;}
static inline void* dlsym(void* h,const char* name){(void)h;(void)name;errno=ENOSYS;return NULL;}
static inline int dlclose(void* h){(void)h;errno=ENOSYS;return -1;}
static inline const char* dlerror(void){return "Dynamic loading unavailable in static ESP-IDF build";}

typedef struct {const char* dli_fname;void* dli_fbase;const char* dli_sname;void* dli_saddr;} Dl_info;
static inline int dladdr(const void* address,Dl_info* info){(void)address;(void)info;errno=ENOSYS;return 0;}
