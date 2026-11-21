#include "clipboard_text.hpp"
#include <cassert>
using namespace tabremoto;
int main(){
 std::vector<uint8_t> wire;std::string decoded;
 for(auto text:{std::string(""),std::string("Olá, ação: ç ü Ü á\r\n😀 🇧🇷"),std::string(4096,'a')}){
  assert(clipboard_utf16(text,wire));assert(clipboard_utf8(wire.data(),wire.size(),decoded));assert(decoded==text);
 }
 assert(!clipboard_utf16(std::string(4097,'a'),wire));assert(!clipboard_utf16(std::string("a\0b",3),wire));
 for(auto text:{std::string("\xc0\x80"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe2\x82")})assert(!clipboard_utf16(text,wire));
 uint8_t odd[]={0,0,0},surrogate[]={0,0xd8,0,0},trailing[]={0,0,'a',0},nonul[]={'a',0};
 assert(!clipboard_utf8(odd,sizeof(odd),decoded));assert(!clipboard_utf8(surrogate,sizeof(surrogate),decoded));assert(!clipboard_utf8(trailing,sizeof(trailing),decoded));assert(!clipboard_utf8(nonul,sizeof(nonul),decoded));
}
