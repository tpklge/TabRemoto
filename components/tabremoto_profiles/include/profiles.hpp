#pragma once
#include <string>
#include <vector>
namespace tabremoto {
struct SavedWifi {std::string ssid,password;bool protected_network=false;~SavedWifi(){volatile char *p=password.data();for(size_t i=0;i<password.size();++i)p[i]=0;}};
void profiles_start();
bool profiles_ready();
std::string profiles_message();
std::vector<SavedWifi> profiles_wifi();
bool profiles_save_wifi(const std::string&,const std::string&,bool);
std::string profiles_pin(const std::string&,const std::string&,unsigned);
bool profiles_save_pin(const std::string&,const std::string&,unsigned,const std::string&);
}
