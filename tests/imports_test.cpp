#include "imports.hpp"
#include "network_routes.hpp"
#include <cassert>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace tabremoto;
int main(){
 const std::string base="/private/tmp/tabremoto-import-policy-test";assert(mkdir(base.c_str(),0700)==0);auto root=import_roots()[0];assert(root==base+"/imports");assert(mkdir(root.c_str(),0700)==0);
 assert(import_path(root+"/test.pem"));assert(!import_path(root+"/../vault/store.a"));assert(!import_path(root+"-evil/test.pem"));assert(!import_path(root+"//test.pem"));assert(!import_path(root+std::string("/bad\0name",9)));
 {std::ofstream f(root+"/test.pem");f<<"private test text\n";}{std::ofstream f(root+"/too-large.conf");f<<std::string(8193,'x');}{std::ofstream f(root+"/ignored.txt");f<<"not listed";}
 assert(symlink("/etc",(root+"/linked").c_str())==0);assert(!import_path(root+"/linked/passwd"));
 std::vector<ImportEntry> list;std::string data,error;assert(import_list(root,{".pem"},list,error));assert(list.size()==1&&list[0].name=="test.pem");assert(import_read(root+"/test.pem",data,error)&&data=="private test text\n");assert(!import_read(root+"/too-large.conf",data,error));assert(!import_read(root+"/../vault/store.a",data,error));
 unlink((root+"/test.pem").c_str());unlink((root+"/too-large.conf").c_str());unlink((root+"/ignored.txt").c_str());unlink((root+"/linked").c_str());rmdir(root.c_str());rmdir(base.c_str());
 assert(network_routes_overlap({{0,0}},{{0x0a570000,0xffff0000}}));assert(network_routes_overlap({{0x0a570000,0xffff0000}},{{0x0a570100,0xffffff00}}));assert(!network_routes_overlap({{0x0a570000,0xffff0000}},{{0x0a580000,0xffff0000}}));
}
