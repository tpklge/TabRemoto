#include "ncm_frames.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
int main(){
 uint8_t packet[1514]={1},block[1664];
 for(unsigned div:{1,4,8,16,64})for(unsigned rem=0;rem<div;rem++){
  auto n=ncm_encode(block,sizeof(block),packet,sizeof(packet),65535,div,rem,4);assert(n);assert(ncm_decode(block,n,nullptr,nullptr)==1);
  unsigned ndp=ncm_read16(block+10);assert(ncm_read16(block+ndp+8)%div==rem);
  ncm_write16(block+ndp+6,ndp);assert(ncm_decode(block,n,nullptr,nullptr)==-1);
 }
 std::mt19937 random(391);for(unsigned i=0;i<30000;i++){unsigned n=random()%sizeof(block);for(unsigned j=0;j<n;j++)block[j]=random();ncm_decode(block,n,nullptr,nullptr);}
 puts("NCM16: framing, negotiated alignment, cyclic tables and malformed input passed");
}
