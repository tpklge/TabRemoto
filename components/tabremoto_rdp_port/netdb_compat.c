#include "openssl_idf_compat.h"
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
const char* tabremoto_gai_strerror(int e){return e==0?"success":"lwIP address lookup failed";}
struct servent* tabremoto_getservbyname(const char* name,const char* protocol){(void)name;(void)protocol;errno=ENOSYS;return NULL;}
int tabremoto_getnameinfo(const struct sockaddr* sa,socklen_t len,char* host,socklen_t hn,char* service,socklen_t sn,int flags){
 (void)flags;const void* addr=NULL;unsigned port=0;
 if(sa&&sa->sa_family==AF_INET&&len>=sizeof(struct sockaddr_in)){const struct sockaddr_in* a=(const void*)sa;addr=&a->sin_addr;port=ntohs(a->sin_port);}
 else if(sa&&sa->sa_family==AF_INET6&&len>=sizeof(struct sockaddr_in6)){const struct sockaddr_in6* a=(const void*)sa;addr=&a->sin6_addr;port=ntohs(a->sin6_port);}
 else return EAI_FAMILY;
 if(host&&(!hn||!inet_ntop(sa->sa_family,addr,host,hn)))return EAI_FAIL;
 if(service){int n=snprintf(service,sn,"%u",port);if(n<0||(unsigned)n>=sn)return EAI_FAIL;}
 return 0;
}
