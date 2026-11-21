#include "vault.hpp"
#include "openssl/evp.h"
#include <cstring>
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
using namespace tabremoto;
static std::string bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
int main(){
 unsigned char salt[16],derived[32];for(unsigned i=0;i<16;i++)salt[i]=i;const unsigned char expected[]={0x4c,0x31,0x73,0x41,0x25,0xa1,0x5e,0xca,0x99,0xc9,0x7a,0xd4,0x94,0x03,0x45,0x9d,0x4f,0x05,0xc5,0x4a,0x78,0x77,0x0d,0x4e,0x65,0x11,0x86,0x1b,0x83,0x49,0xc7,0xc5};assert(EVP_PBE_scrypt("Ab123456",8,salt,16,16384,8,1,24ULL*1024*1024,derived,32)==1);assert(memcmp(expected,derived,32)==0);
 char temp[]="/tmp/tabremoto-vault-XXXXXX";assert(mkdtemp(temp));std::string root=temp;
 const std::string password="Tab58221",secret_value="Private-test-secret-9381";
 Vault v(root);assert(!v.exists());assert(!v.create("short"));assert(!v.create("1234567!"));assert(v.create(password));assert(v.unlocked());assert(!v.create(password));
 std::string secret,identity,conn;std::string rejected;assert(!v.secret(rejected,"Too large","key",std::string(8193,'x')));rejected.clear();assert(!v.secret(rejected,"Bad kind","unsupported","x"));assert(v.secret(secret,"Test key","key",secret_value));
 assert(v.identity(identity,"Admin","administrator","LAB",secret,"key"));
 assert(v.connection(conn,"Favorite","ssh","SERVER.Example.",22,identity,true));
 assert(!v.remove("identities",identity));assert(!v.remove("secrets",secret));
 std::string invalid;assert(!v.connection(invalid,"Bad","ssh","",22,identity,false));assert(!v.connection(invalid,"Bad","ssh","host",0,identity,false));
 std::string a(64,'a'),b(64,'b');assert(v.check_trust("ssh","server.example",22,a)==0);
 assert(v.trust("ssh","server.example",22,a,"","Lab fingerprint verified"));
 assert(v.check_trust("ssh","SERVER.EXAMPLE.",22,a)==1);assert(v.check_trust("ssh","server.example",2222,a)==0);assert(v.check_trust("rdp","server.example",22,a)==0);
 assert(v.check_trust("ssh","server.example",22,b)==2);assert(!v.trust("ssh","server.example",22,b,"","Wrong expected key"));
 assert(v.trust("ssh","server.example",22,b,a,"Rotation verified"));
 std::string vpn,protected_conn;
 assert(v.secret(vpn,"Test tunnel","vpn","[Interface]\nPrivateKey=test"));
 assert(v.connection(protected_conn,"Protected","ssh","inside.example",22,identity,false,"","wifi",vpn,true));
 assert(!v.remove("secrets",vpn));
 std::string sh,su,sk,sv,sp,st,sr; int port=0;bool required=false;
 assert(v.session_data(protected_conn,sh,port,su,sk,sv,sp,st,sr,required));
 assert(required && st=="wifi" && sr==vpn && port==22);
 auto policy_export=v.public_catalog();
 assert(policy_export.find("\"vpn_required\":true")!=std::string::npos);
 assert(policy_export.find(vpn)==std::string::npos);
 assert(v.remove("connections",protected_conn));assert(v.remove("secrets",vpn));
 std::string missing_identity;
 assert(v.connection(missing_identity,"Without identity","ssh","host.example",22,"",false));
 assert(!v.session_data(missing_identity,sh,port,su,sk,sv,sp,st,sr,required));
 assert(v.error.find("Vincule uma identidade")!=std::string::npos);
 assert(v.remove("connections",missing_identity));

 std::string rdp_secret,rdp_identity,rdp_connection,domain;
 assert(v.secret(rdp_secret,"RDP password","password","Only-test-password"));
 assert(v.identity(rdp_identity,"RDP admin","Administrator","LAB",rdp_secret,"password"));
 assert(v.connection(rdp_connection,"RDP test","rdp","server.example",3389,rdp_identity,false));
 assert(v.trust("rdp","server.example",3389,a,"","Certificate independently checked"));
 assert(!v.session_data(rdp_connection,sh,port,su,sk,sv,sp,st,sr,required));
 assert(v.session_data(rdp_connection,sh,port,su,sk,sv,sp,st,sr,required,"rdp",&domain));
 assert(port==3389&&domain=="LAB"&&sp==a&&sk=="password"&&sv=="Only-test-password");
 std::string isolated;assert(v.connection(isolated,"No RDP trust","rdp","server.example",3390,rdp_identity,false));
 assert(v.trust("ssh","server.example",3390,b,"","SSH only pin"));sp=b;
 assert(v.session_data(isolated,sh,port,su,sk,sv,sp,st,sr,required,"rdp",&domain));assert(sp.empty());assert(v.remove("connections",isolated));
 assert(!v.session_data(conn,sh,port,su,sk,sv,sp,st,sr,required,"rdp",&domain));
 assert(v.remove("connections",rdp_connection));assert(v.remove("identities",rdp_identity));assert(v.remove("secrets",rdp_secret));
 auto exported=v.metadata();assert(exported.find(secret_value)==std::string::npos);assert(exported.find("Favorite")!=std::string::npos);
 for(auto suffix:{"/store.a","/store.b"}){auto raw=bytes(root+suffix);assert(raw.find(secret_value)==std::string::npos);assert(raw.find("administrator")==std::string::npos);}
 auto public_data=v.public_catalog();assert(public_data.find(secret_value)==std::string::npos);assert(public_data.find(secret)==std::string::npos);
 std::string importroot=root+"/import";assert(mkdir(importroot.c_str(),0700)==0);
 {Vault imported(importroot);assert(imported.create("Copia123"));assert(imported.import_catalog(public_data));assert(imported.count("connections")==1&&imported.count("secrets")==0);assert(!imported.import_catalog(public_data));assert(!imported.import_catalog("{\"schema\":999,\"connections\":[],\"identities\":[]}"));assert(imported.count("connections")==1);}
 unlink((importroot+"/store.a").c_str());unlink((importroot+"/store.b").c_str());rmdir(importroot.c_str());
 assert(v.backup_encrypted(root+"/backup.trv"));assert(!v.backup_encrypted(root+"/backup.trv"));assert(bytes(root+"/backup.trv").find(secret_value)==std::string::npos);unlink((root+"/backup.trv").c_str());
 auto gen=v.generation();v.lock();assert(v.metadata().empty());assert(!v.remove("connections",conn));assert(!v.unlock("Erro0000"));assert(v.unlock(password));assert(v.generation()==gen&&v.count("connections")==1);
 assert(!v.change_password("short"));assert(v.change_password("Nova8872"));v.lock();assert(!v.unlock(password));assert(v.unlock("Nova8872"));
 assert(v.remove("connections",conn));assert(v.remove("identities",identity));assert(v.remove("secrets",secret));
 // Creating an identity can bind a host atomically, without a schema change.
 std::string host_target,linked;assert(v.connection(host_target,"Bind host","ssh","bind.example",22,"",false));
 assert(v.identity(linked,"Bound identity","bound-user","","","ask",host_target));
 assert(v.session_data(host_target,sh,port,su,sk,sv,sp,st,sr,required));assert(su=="bound-user");
 auto bound_before=v.metadata();std::string invalid_link;assert(!v.identity(invalid_link,"Invalid bind","user","","","ask","not-a-host"));assert(v.metadata()==bound_before);
 assert(rename(root.c_str(),(root+"-bind-moved").c_str())==0);std::string uncommitted;
 assert(!v.identity(uncommitted,"Rollback bind","other-user","","","ask",host_target));assert(v.metadata()==bound_before);
 assert(rename((root+"-bind-moved").c_str(),root.c_str())==0);
 assert(v.remove("connections",host_target));assert(v.remove("identities",linked));
 // Damage both generations independently: normal unlock fails closed; recovery
 // may authenticate an older valid generation only when explicitly requested.
 auto xa=bytes(root+"/store.a"),xb=bytes(root+"/store.b");
 auto genof=[](const std::string& x){return (unsigned char)x[12]|((unsigned char)x[13]<<8);};
 auto latest=genof(xa)>genof(xb)?"/store.a":"/store.b";auto original=bytes(root+latest);auto changed=original;changed.back()^=1;
 {std::ofstream f(root+latest,std::ios::binary);f.write(changed.data(),changed.size());}
 v.lock();assert(!v.unlock("Nova8872"));assert(v.unlock("Nova8872",true));assert(bytes(root+latest)==changed);
 v.lock();{std::ofstream f(root+latest,std::ios::binary);f.write(original.data(),original.size());}
 assert(v.unlock("Nova8872"));
 // Simulate write failure without touching committed data.
 assert(rename(root.c_str(),(root+"-moved").c_str())==0);std::string fail;
 assert(!v.connection(fail,"Uncommitted","rdp","host",3389,"",true));assert(v.count("connections")==0);
 assert(rename((root+"-moved").c_str(),root.c_str())==0);v.lock();assert(v.unlock("Nova8872"));assert(v.count("connections")==0);
 // Authenticated restore keeps current password and rolls back failed writes.
 {
  auto srcdir=root+"/restore-source",destdir=root+"/restore-target",backup=root+"/restore.trv";
  assert(mkdir(srcdir.c_str(),0700)==0&&mkdir(destdir.c_str(),0700)==0);
  Vault src(srcdir),dst(destdir);assert(src.create("Orig1234")&&dst.create("Curr1234"));
  std::string sid,iid,cid,other;assert(src.secret(sid,"Restored key","key","Only-restore-fixture"));
  assert(src.identity(iid,"Restored identity","restore-user","",sid,"key"));
  assert(src.connection(cid,"Restored host","ssh","restored.example",22,iid,false));assert(src.backup_encrypted(backup));
  assert(dst.connection(other,"Current host","ssh","current.example",22,"",false));
  auto before=dst.metadata(),ca=bytes(destdir+"/store.a"),cb=bytes(destdir+"/store.b");
  assert(!dst.restore_encrypted(backup,"Wrong123"));assert(dst.metadata()==before&&bytes(destdir+"/store.a")==ca&&bytes(destdir+"/store.b")==cb);
  auto authentic=bytes(backup),bad=authentic;bad.back()^=1;{std::ofstream f(backup,std::ios::binary);f.write(bad.data(),bad.size());}
  assert(!dst.restore_encrypted(backup,"Orig1234"));assert(dst.metadata()==before);{std::ofstream f(backup,std::ios::binary);f.write(authentic.data(),authentic.size());}
  assert(rename(destdir.c_str(),(destdir+"-moved").c_str())==0);
  assert(!dst.restore_encrypted(backup,"Orig1234"));assert(dst.metadata()==before);
  assert(rename((destdir+"-moved").c_str(),destdir.c_str())==0);assert(bytes(destdir+"/store.a")==ca&&bytes(destdir+"/store.b")==cb);
  assert(dst.restore_encrypted(backup,"Orig1234"));assert(dst.metadata().find("Restored host")!=std::string::npos&&dst.metadata().find("Current host")==std::string::npos);
  dst.lock();assert(!dst.unlock("Orig1234"));assert(dst.unlock("Curr1234"));std::string restored_kind,restored_value;assert(dst.secret_data(sid,restored_kind,restored_value)&&restored_value=="Only-restore-fixture");
  dst.lock();assert(!dst.restore_encrypted(backup,"Orig1234"));
  for(auto dir:{srcdir,destdir}){unlink((dir+"/store.a").c_str());unlink((dir+"/store.b").c_str());rmdir(dir.c_str());}unlink(backup.c_str());
 }
 unlink((root+"/store.a").c_str());unlink((root+"/store.b").c_str());rmdir(root.c_str());puts("Vault host tests passed");
}
