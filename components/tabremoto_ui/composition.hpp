#pragma once
#include <string>
namespace tabremoto {
struct Composition {
 char pending=0;
 void reset(){pending=0;}
 std::string feed(int key,bool alt){
  if(key==8&&pending){reset();return {};}
  if(key<32||key>126)return {};
  char c=char(key);
  // US-International style: use keys printed on A164 directly. No Alt chord
  // is needed. Space or the same quote twice produces a literal quote.
  if(!alt&&(c=='\''||c=='`'||c=='^'||c=='~'||c=='"')){
   if(pending==c){reset();return std::string(1,c);}
   if(pending){char previous=pending;pending=c;return std::string(1,previous);}
   pending=c;return {};
  }
  if(alt){
   if(c=='c')return "ç";
   if(c=='C')return "Ç";
   if(c=='\''||c=='`'||c=='^'||c=='~'||c=='"'){pending=c;return {};}
   return {};
  }
  if(!pending)return std::string(1,c);
  char accent=pending;reset();
  if(c==' ')return std::string(1,accent);
  if(accent=='\''&&(c=='c'||c=='C'))return c=='c'?"ç":"Ç";
  const char* base="aAeEiIoOuUnNcC";
  const char* acute[]={"á","Á","é","É","í","Í","ó","Ó","ú","Ú",nullptr,nullptr,nullptr,nullptr};
  const char* grave[]={"à","À","è","È","ì","Ì","ò","Ò","ù","Ù",nullptr,nullptr,nullptr,nullptr};
  const char* circum[]={"â","Â","ê","Ê","î","Î","ô","Ô","û","Û",nullptr,nullptr,nullptr,nullptr};
  const char* tilde[]={"ã","Ã",nullptr,nullptr,nullptr,nullptr,"õ","Õ",nullptr,nullptr,"ñ","Ñ",nullptr,nullptr};
  const char* dia[]={"ä","Ä","ë","Ë","ï","Ï","ö","Ö","ü","Ü",nullptr,nullptr,nullptr,nullptr};
  auto table=accent=='\''?acute:accent=='`'?grave:accent=='^'?circum:accent=='~'?tilde:dia;
  for(unsigned i=0;base[i];i++)if(base[i]==c&&table[i])return table[i];
  return std::string(1,accent)+c;
 }
};
}
