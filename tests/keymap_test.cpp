#include "../components/tabremoto_ui/keymap.hpp"
#include <cassert>
#include "../components/tabremoto_ui/composition.hpp"
int main(){tabremoto::KeyMap m;assert(m.feed(0xb2)=='a');assert(m.feed(0x32)==0);m.feed(0xb1);assert(m.feed(0xb2)=='A');m.feed(0x31);m.feed(0xc0);assert(m.feed(0xc4)==3);m.feed(0x40);assert(m.feed(0xc4)=='c');assert(m.feed(0xad)==8);assert(m.feed(255)==0);assert(!m.ctrl&&!m.shift);m.feed(0xb0);assert(m.feed(0x9b)=='{');assert(m.feed(0x9c)=='}');assert(m.feed(0xff)==0);}
struct RepeatTests{RepeatTests(){using namespace tabremoto;KeyRepeat r;assert(r.event(0xb2,0)=='a');assert(r.tick(449)==0);assert(r.tick(450)=='a');assert(r.tick(504)==0);assert(r.tick(505)=='a');r.event(0x32,506);assert(r.tick(1000)==0);r.event(0xca,1000);assert(r.tick(1450)==Left);r.event(255,1451);assert(r.tick(2000)==0);KeyMap m;assert(m.feed(0x8d)==Delete);assert(m.feed(0xbb)==Up);assert(m.feed(0xcb)==Down);assert(m.feed(0xcc)==Right);m.feed(0xc0);assert(m.feed(0xca)==Home);assert(m.feed(0xcc)==End);}} repeat_tests;
struct CompositionTests{CompositionTests(){tabremoto::Composition c;assert(c.feed('\'',true).empty());assert(c.feed('a',false)=="á");c.feed('~',true);assert(c.feed('A',false)=="Ã");assert(c.feed('c',true)=="ç");c.feed('^',true);assert(c.feed('o',false)=="ô");c.feed('`',true);assert(c.feed('a',false)=="à");c.feed('"',true);assert(c.feed('u',false)=="ü");c.feed('\'',true);assert(c.feed('x',false)=="'x");c.feed('\'',true);c.feed(8,false);assert(c.feed('a',false)=="a");c.feed('~',true);c.reset();assert(c.feed('o',false)=="o");}} composition_tests;
struct PhysicalAccentTests{PhysicalAccentTests(){tabremoto::KeyRepeat k;tabremoto::Composition c;auto key=[&](uint8_t raw){return c.feed(k.event(raw,0),k.map.alt);};assert(key(0xac).empty());key(0x2c);assert(key(0xb2)=="á");key(0x32);key(0xac);key(0x2c);assert(key(0xc4)=="ç");key(0x44);key(0xac);key(0x2c);assert(key(0xcd)=="'");key(0x4d);assert(key(0xac).empty());key(0x2c);assert(key(0xac)=="'");key(0x2c);key(0xac);key(0x2c);key(0xb1);assert(key(0xc4)=="Ç");key(0x44);key(0x31);}} physical_accent_tests;

struct DiaeresisTests { DiaeresisTests(){
 using namespace tabremoto;KeyRepeat k;Composition c;
 auto key=[&](uint8_t raw){return c.feed(k.event(raw,0),k.map.alt);};
 const uint8_t vowels[]={0xb2,0xa3,0xa8,0xa9,0xa7};
 const char* lower[]={"ä","ë","ï","ö","ü"};const char* upper[]={"Ä","Ë","Ï","Ö","Ü"};
 for(unsigned i=0;i<5;i++){
  key(0xb1);assert(key(0xac).empty());key(0x2c);key(0x31);
  assert(key(vowels[i])==lower[i]);key(vowels[i]&127);
  key(0xb1);key(0xac);key(0x2c);assert(key(vowels[i])==upper[i]);key(vowels[i]&127);key(0x31);
 }
 key(0xb1);key(0xac);key(0x2c);key(0x31);assert(key(0xcd)=="\"");key(0x4d);
 key(0xb1);key(0xac);key(0x2c);assert(key(0xac)=="\"");key(0x2c);key(0x31);
 }} diaeresis_tests;

struct FunctionKeys { FunctionKeys(){using namespace tabremoto;KeyMap map;const char* normal="1234567890-+";for(int i=1;i<=12;i++){assert(map.feed(0x80+i)==normal[i-1]);map.feed(i);map.feed(0xb1);assert(map.feed(0x80+i)==F1+i-1);map.feed(i);map.feed(0x31);}map.feed(0xb1);assert(map.feed(0xb2)=='A');map.feed(255);assert(map.feed(0x81)=='1');}} function_keys;
