#include "touchpad.hpp"
#include <cassert>
#include <tuple>
#include <vector>
using namespace tabremoto;
int main(){
 Touchpad t;std::vector<std::tuple<int,int,unsigned>> events;
 auto send=[&](int x,int y,unsigned f){events.emplace_back(x,y,f);};
 t.geometry(1280,720,true);t.event(Touchpad::Press,100,100,0,send);
 t.event(Touchpad::Move,160,130,100,send);t.event(Touchpad::Release,160,130,200,send);
 assert(t.x==700&&t.y==390&&events.size()==1&&std::get<2>(events[0])==0x0800);
 events.clear();t.event(Touchpad::Press,20,20,300,send);t.event(Touchpad::Release,20,20,400,send);
 assert(events.size()==2&&std::get<0>(events[0])==700&&std::get<2>(events[0])==0x9000&&std::get<2>(events[1])==0x1000);
 events.clear();t.event(Touchpad::Press,0,0,500,send);t.event(Touchpad::Move,20,20,1100,send);
 assert(t.dragging());t.event(Touchpad::Cancel,0,0,1150,send);assert(!t.dragging()&&std::get<2>(events.back())==0x1000);
 events.clear();t.event(Touchpad::Press,0,0,1200,send);t.event(Touchpad::Move,30,0,1300,send);t.event(Touchpad::Move,100,0,2000,send);t.event(Touchpad::Release,100,0,2100,send);
 assert(events.size()==2);for(auto e:events)assert(std::get<2>(e)==0x0800);
 t.event(Touchpad::Press,0,0,2200,send);t.event(Touchpad::Move,9000,-9000,2300,send);t.event(Touchpad::Release,9000,-9000,2400,send);assert(t.x==1279&&t.y==0);
 events.clear();t.event(Touchpad::Press,100,100,2500,send);
 for(unsigned i=1;i<25;++i)t.event(Touchpad::Move,100+int(i%3)-1,100,2500+i*5,send);
 t.event(Touchpad::Release,100,100,2650,send);
 assert(std::get<2>(events[events.size()-2])==0x9000&&std::get<2>(events.back())==0x1000);
 events.clear();t.event(Touchpad::Press,100,100,2700,send);t.event(Touchpad::Release,100,100,3600,send);assert(events.size()==2);
 events.clear();int oldx=t.x,oldy=t.y;
 t.event(Touchpad::Press,400,300,4000,send);t.event(Touchpad::Move,413,307,4130,send);t.event(Touchpad::Move,406,312,4300,send);t.event(Touchpad::Release,407,305,4480,send);
 assert(events.size()==2&&t.x==oldx&&t.y==oldy);assert(std::get<2>(events[0])==0x9000&&std::get<2>(events[1])==0x1000);
 events.clear();unsigned clicks=0;t.event_with_click(Touchpad::Press,300,200,5000,send,[&](int,int){++clicks;});
 t.event_with_click(Touchpad::Release,304,206,5700,send,[&](int,int){++clicks;});assert(clicks==1&&events.empty());
 events.clear();oldx=t.x;oldy=t.y;
 t.event(Touchpad::Press,500,300,6000,send);t.event(Touchpad::Move,513,307,6650,send);t.event(Touchpad::Move,506,312,6800,send);t.event(Touchpad::Release,507,305,7050,send);
 assert(events.size()==2&&t.x==oldx&&t.y==oldy);assert(std::get<2>(events[0])==0x9000&&std::get<2>(events[1])==0x1000);
 t.geometry(800,450);assert(t.x==799&&t.y==0);
}
