#include "wireguard_inner.h"
#include <array>
#include <cassert>
int main(){
 std::array<uint8_t,96> p{};unsigned family;uint8_t source[16];size_t length;
 p[0]=0x60;p[4]=0;p[5]=20;p[8]=0xfd;p[23]=0x11;
 assert(wireguard_inner_packet(p.data(),64,&family,source,&length));
 assert(family==6 && length==60 && source[0]==0xfd && source[15]==0x11);
 assert(!wireguard_inner_packet(p.data(),59,&family,source,&length));
 for(size_t n=0;n<40;n++)assert(!wireguard_inner_packet(p.data(),n,&family,source,&length));
 p[4]=1;p[5]=0;assert(!wireguard_inner_packet(p.data(),96,&family,source,&length));
 p.fill(0);p[0]=0x45;p[3]=40;p[12]=10;p[15]=7;
 assert(wireguard_inner_packet(p.data(),48,&family,source,&length));assert(family==4&&length==40&&source[0]==10&&source[3]==7&&source[4]==0);
 p[3]=19;assert(!wireguard_inner_packet(p.data(),48,&family,source,&length));
 p[3]=40;p[0]=0x4f;assert(!wireguard_inner_packet(p.data(),48,&family,source,&length));
 p[0]=0x75;assert(!wireguard_inner_packet(p.data(),48,&family,source,&length));
}
