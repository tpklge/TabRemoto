#pragma once
#include <algorithm>
#include <cstdint>
namespace tabremoto {
// Coordinates are physical display pixels. Keep finger jitter out of a tap.
class Touchpad {
public:
 enum Event { Press, Move, Release, Cancel };
 int x=640,y=360;
 void geometry(unsigned width,unsigned height,bool center=false) {
  w=std::max(1u,width);h=std::max(1u,height);
  if(center){x=w/2;y=h/2;}
  x=std::clamp(x,0,int(w)-1);y=std::clamp(y,0,int(h)-1);
 }
 template<class Send,class Click> void event_with_click(Event event,int px,int py,uint32_t now,Send send,Click click) {
  if(event==Press){active=true;drag=false;moved=false;eligible=true;last_x=start_x=px;last_y=start_y=py;started=now;return;}
  if(!active)return;
  if(event==Cancel){if(drag)send(x,y,0x1000);active=drag=false;return;}
  int dx=px-last_x,dy=py-last_y;last_x=px;last_y=py;
  int sx=px-start_x,sy=py-start_y;
  bool stroke=int64_t(sx)*sx+int64_t(sy)*sy>24*24;
  bool was_moving=moved;
  if(stroke){moved=true;if(now-started<500)eligible=false;}
  // A deliberate move after a hold starts a drag. A normal stroke never does.
  if(event==Move && !drag && eligible && now-started>=500 && stroke){
   drag=true;moved=true;send(x,y,0x9000);
  }
  if(moved){
   if(!was_moving){dx=sx;dy=sy;}
   if(dx||dy){x=std::clamp(x+dx,0,int(w)-1);y=std::clamp(y+dy,0,int(h)-1);send(x,y,0x0800);}
  }
  if(event==Release){
   if(drag)send(x,y,0x1000);
   else if(!moved)click(x,y);
   active=drag=false;
  }
 }
 template<class Send> void event(Event event,int px,int py,uint32_t now,Send send){
  event_with_click(event,px,py,now,send,[&](int cx,int cy){send(cx,cy,0x9000);send(cx,cy,0x1000);});
 }
 bool dragging()const{return drag;}
private:
 unsigned w=1280,h=720;int last_x=0,last_y=0,start_x=0,start_y=0;
 uint32_t started=0;bool active=false,drag=false,moved=false,eligible=true;
};
}
