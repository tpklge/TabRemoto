#pragma once
#include <cstdint>
namespace tabremoto {
enum EditKey { Left=0x10000,Right,Up,Down,Delete,Home,End,F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12 };
// A164 physical Normal matrix. Aa acts as a held Shift in this first map.
struct KeyMap {
 bool shift=false,ctrl=false,alt=false,sym=false;
 int feed(uint8_t event){
  if(event==255){shift=ctrl=alt=sym=false;return 0;}
  unsigned row=(event>>4)&7,col=event&15;bool down=event&128;
  if(row>=5||col>=14)return 0;
  if(row==3&&col==1){shift=down;return 0;}
  if(row==3&&col==0){sym=down;return 0;}
  if(row==4&&col==0){ctrl=down;return 0;}
  if(row==4&&col==1){alt=down;return 0;}
  if(!down)return 0;
  if(shift&&row==0&&col>=1&&col<=12)return F1+col-1;
  if(row==0&&col==13)return Delete;
  if(row==3&&col==11)return Up;
  if(row==4&&col==10)return ctrl?Home:Left;
  if(row==4&&col==11)return Down;
  if(row==4&&col==12)return ctrl?End:Right;
  if(row==2&&col==13)return 8;
  if(row==3&&col==13)return 10;
  if(row==0&&col==0)return 27;
  if(row==2&&col==0)return 9;
  if(row==4&&col==13)return ' ';
  static const char* rows[]={"\0" "1234567890-+", "`!@#$%^&*()[]\\", "\0qwertyuiop;'", "\0\0asdfghjkl\0_", "\0\0zxcvbnm."};
  unsigned lengths[]={13,14,13,13,10};
  if(col>=lengths[row])return 0;
  char ch=rows[row][col];
  if(ch>='a'&&ch<='z'){if(ctrl)return ch-'a'+1;if(shift)ch-=32;}
  else if(shift||sym){if(row==1){static const char shifted[]="~?@#$%^&/<>{}|";ch=shifted[col];}else if(ch==';')ch=':';else if(ch=='\'')ch='"';else if(ch=='.')ch=',';else if(ch=='_')ch='=';}
  return ch;
 }
};
struct KeyRepeat {
 KeyMap map;
 uint8_t held=255;
 uint32_t next=0;
 static bool repeatable(int key){return key==8 || (key>=32&&key<127) || (key>=Left&&key<=End);}
 int event(uint8_t raw,uint32_t now){
  if(raw==255){held=255;return map.feed(raw);}
  int key=map.feed(raw);
  if(!(raw&128)){if((held&127)==raw)held=255;return key;}
  if(key){held=repeatable(key)?raw:255;next=now+450;}
  return key;
 }
 int tick(uint32_t now){
  if(held==255||int32_t(now-next)<0)return 0;
  next=now+55;
  int key=map.feed(held);
  return repeatable(key)?key:0;
 }
};
}
