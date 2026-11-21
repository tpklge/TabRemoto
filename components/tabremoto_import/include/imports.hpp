#pragma once
#include <string>
#include <vector>
namespace tabremoto {
struct ImportEntry {std::string name,path;bool directory=false;};
// Caller owns an SD lease; paths are confined to explicit application import/backup roots.
const std::vector<std::string>& import_roots();
bool import_path(const std::string& path);
bool import_list(const std::string& directory,const std::vector<std::string>& extensions,std::vector<ImportEntry>& entries,std::string& error);
bool import_read(const std::string& path,std::string& data,std::string& error);
}
