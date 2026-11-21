#include "rdp_damage.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace tabremoto;
int main(){
 RdpDamage a{-3,-4,8,9};a=a.clipped(20,20);assert(a.x==0&&a.y==0&&a.w==5&&a.h==5);
 a.add({10,12,3,4});assert(a.w==13&&a.h==16);assert((RdpDamage{22,22,4,4}.clipped(20,20).empty()));
 alignas(4) std::array<uint8_t,4*16*9> src{};alignas(4) std::array<uint8_t,2*16*9> dst{};
 for(unsigned i=0;i<16*9;++i){src[i*4]=i*11;src[i*4+1]=i*7;src[i*4+2]=i*3;}
 dst.fill(0xa5);rdp_copy_rgb565(dst.data(),32,src.data(),64,{3,2,7,4});
 for(unsigned y=0;y<9;++y)for(unsigned x=0;x<16;++x){uint16_t v;memcpy(&v,dst.data()+y*32+x*2,2);if(x>=3&&x<10&&y>=2&&y<6){auto *p=src.data()+y*64+x*4;assert(v==uint16_t((p[2]>>3)*2048+(p[1]>>2)*32+(p[0]>>3)));}else assert(v==0xa5a5);}
 uint8_t colors[]={0,0,255,0,0,255,0,0,255,0,0,0,255,255,255,0};uint16_t out[4]{};
 rdp_copy_rgb565(reinterpret_cast<uint8_t*>(out),8,colors,16,{0,0,4,1});assert(out[0]==0xf800&&out[1]==0x7e0&&out[2]==0x1f&&out[3]==0xffff);
 std::array<uint8_t,32*9> direct{};direct.fill(0xcc);
 rdp_copy_rgb565_surface(direct.data(),32,dst.data(),32,{3,2,7,4});
 for(unsigned y=0;y<9;++y)for(unsigned x=0;x<16;++x){uint16_t v;memcpy(&v,direct.data()+y*32+x*2,2);if(x>=3&&x<10&&y>=2&&y<6){uint16_t expected;memcpy(&expected,dst.data()+y*32+x*2,2);assert(v==expected);}else assert(v==0xcccc);}

}
