#pragma once
#include <stddef.h>
struct sockaddr;
struct servent {char *s_name;char **s_aliases;int s_port;char *s_proto;};
const char* tabremoto_gai_strerror(int error);
struct servent* tabremoto_getservbyname(const char* name,const char* protocol);
int tabremoto_getnameinfo(const struct sockaddr*,unsigned long,char*,unsigned long,char*,unsigned long,int);
#define gai_strerror tabremoto_gai_strerror
#define getservbyname tabremoto_getservbyname
#define getnameinfo tabremoto_getnameinfo
