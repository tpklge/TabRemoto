#include "terminal.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
using tabremoto::Terminal;
void feed(Terminal& t,const char* p){t.feed(p,strlen(p));}
int main(){
 Terminal t;t.feed("a\xc3",2);t.feed("\xa7",1);assert(t.text().find("aç")==0);
 feed(t,"\rX");assert(t.text().find("Xç")==0);
 feed(t,"\033[2J\033[Hok\033]52;c;IGNORAR\007!");assert(t.text().find("ok!")==0);assert(t.text().find("IGNORAR")==std::string::npos);
 feed(t,"\033[2;3HZ");assert(t.frame()->cells[102].character=='Z');
 feed(t,"\033[H\033[31;44mR\033[38;2;12;34;56mT\033[0mN");auto f=t.frame();assert(f->cells[0].foreground!=f->cells[2].foreground);assert(f->cells[0].background!=f->cells[2].background);assert(f->cells[1].foreground==0x0c2238);
 feed(t,"\033[?1049h\033[Halternative\033[?1049l");assert(t.frame()->cells[0].character=='R');
 feed(t,"\033[6n");assert(t.replies().find('R')!=std::string::npos);assert(t.replies().empty());
 using tabremoto::TerminalKey;
 t.key(TerminalKey::Up);assert(t.replies()=="\033[A");
 feed(t,"\033[?1h");t.key(TerminalKey::Up);assert(t.replies()=="\033OA");
 t.key(TerminalKey::Down);assert(t.replies()=="\033OB");
 t.key(TerminalKey::F1);assert(t.replies()=="\033OP");t.key(TerminalKey::F10);assert(t.replies()=="\033[21~");
 t.key(TerminalKey::F11);assert(t.replies()=="\033[23~");t.key(TerminalKey::F12);assert(t.replies()=="\033[24~");
 t.key(TerminalKey::Tab,1);assert(t.replies()=="\033[Z");t.key(TerminalKey::Up,4);assert(t.replies()=="\033[1;5A");
 feed(t,"\033[?1l");t.key(TerminalKey::Up);assert(t.replies()=="\033[A");
 Terminal bounded;std::string huge(100000,'x');bounded.feed(huge.data(),huge.size());assert(bounded.frame()->history==Terminal::history_limit);assert(bounded.frame()->cells.size()==(Terminal::rows+Terminal::history_limit)*Terminal::columns);
 std::cout<<"Terminal: UTF-8, cursor, OSC, SGR RGB/palette/reset, alternate screen, query replies and bounded scrollback passed\n";
}
