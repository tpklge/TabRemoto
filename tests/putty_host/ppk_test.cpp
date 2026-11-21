#include "putty_key.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <cstring>
#include <sys/stat.h>
int main(int argc,char** argv){
 assert(argc==3);std::ifstream file(argv[1],std::ios::binary);std::string input((std::istreambuf_iterator<char>(file)),{});char* output=nullptr;size_t size=0;const char* error=nullptr;
 if(!tabremoto_ppk_convert(input.data(),input.size(),"UnitKey42",&output,&size,&error)){std::cerr<<(error?error:"unknown")<<"\n";return 1;}
 assert(size>100&&std::string(output,size).find("-----BEGIN OPENSSH PRIVATE KEY-----\n")==0);
 {std::ofstream converted(argv[2],std::ios::binary);converted.write(output,size);}chmod(argv[2],0600);memset(output,0,size);free(output);
 if(input.find("Encryption: aes256-cbc")!=std::string::npos)assert(!tabremoto_ppk_convert(input.data(),input.size(),"wrong",&output,&size,&error));
 auto mac=input.find("Private-MAC: ");assert(mac!=std::string::npos);input[mac+13]=input[mac+13]=='a'?'b':'a';assert(!tabremoto_ppk_convert(input.data(),input.size(),"UnitKey42",&output,&size,&error));
 std::string invalid="PuTTY-User-Key-File-3: ssh-rsa\nArgon2-Memory: 65536\n";assert(!tabremoto_ppk_convert(invalid.data(),invalid.size(),"",&output,&size,&error));
 std::cout<<"PPK conversion, wrong passphrase, MAC mutation and resource bounds passed\n";
}
