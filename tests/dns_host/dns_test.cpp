#include "dns_resolver.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
using namespace tabremoto;
int main(int argc,char **argv){
 assert(argc==3);std::vector<DnsAddress> addresses;std::string error;unsigned configured=0;
 for(unsigned index:{0u,1u,0u}){
  auto previous=configured;assert(dns_resolve("shared.tabremoto.test",argv[index+1],[&](int){++configured;return true;},addresses,error));assert(configured>previous);
  bool v4=false,v6=false;
  for(auto address:addresses){char text[46]{};assert(inet_ntop(address.family==4?AF_INET:AF_INET6,address.bytes.data(),text,sizeof(text)));v4|=address.family==4&&std::string(text)==(index?"10.90.0.12":"10.90.0.11");v6|=address.family==6&&std::string(text)==(index?"fd79:90::12":"fd79:90::11");}
  assert(v4&&v6);
 }
 unsigned denied=0;assert(!dns_resolve("shared.tabremoto.test",argv[1],[&](int){++denied;return false;},addresses,error));assert(denied>0);
 auto previous=configured;assert(dns_resolve("192.0.2.1",argv[1],[&](int){++configured;return false;},addresses,error));assert(configured==previous&&addresses.size()==1);
 assert(!dns_resolve("bad\nhost",argv[1],[](int){return true;},addresses,error));
 std::cout<<"Independent DNS answers/cache, IPv4/IPv6, and no packet when binding fails passed\n";
}
