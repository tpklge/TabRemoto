#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace tabremoto {
struct RdpDamage {
 int x=0,y=0,w=0,h=0;
 bool empty()const{return w<=0||h<=0;}
 void add(RdpDamage b){if(b.empty())return;if(empty()){*this=b;return;}int right=std::max(x+w,b.x+b.w),bottom=std::max(y+h,b.y+b.h);x=std::min(x,b.x);y=std::min(y,b.y);w=right-x;h=bottom-y;}
 RdpDamage clipped(int width,int height)const{int left=std::clamp(x,0,width),top=std::clamp(y,0,height),right=std::clamp(x+w,0,width),bottom=std::clamp(y+h,0,height);return {left,top,right-left,bottom-top};}
};
inline void rdp_copy_rgb565(uint8_t *dst,unsigned dst_stride,const uint8_t *src,unsigned src_stride,RdpDamage area){
 for(int y=area.y;y<area.y+area.h;++y){auto *to=reinterpret_cast<uint16_t*>(dst+y*dst_stride)+area.x;auto *from=src+y*src_stride+area.x*4;
  for(int x=0;x<area.w;++x,from+=4)*to++=uint16_t(((from[2]&0xf8)<<8)|((from[1]&0xfc)<<3)|(from[0]>>3));
 }
}
inline void rdp_copy_rgb565_surface(uint8_t *dst,unsigned dst_stride,const uint8_t *src,unsigned src_stride,RdpDamage area){
 for(int y=area.y;y<area.y+area.h;++y)
  std::memcpy(dst+y*dst_stride+area.x*2,src+y*src_stride+area.x*2,size_t(area.w)*2);
}

}
