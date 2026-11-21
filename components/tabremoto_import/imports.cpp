#include "imports.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
// FATFS cannot create symbolic links; newlib omits lstat.
#define lstat stat
#endif
namespace tabremoto {
#ifndef TABREMOTO_IMPORT_BASE
#define TABREMOTO_IMPORT_BASE "/sdcard/TabRemoto"
#endif
const std::vector<std::string> &import_roots() {
  static const std::vector<std::string> roots = {
      TABREMOTO_IMPORT_BASE "/imports",
      TABREMOTO_IMPORT_BASE "/transfers/uploads",
      TABREMOTO_IMPORT_BASE "/transfers/downloads",
      TABREMOTO_IMPORT_BASE "/backups"};
  return roots;
}
bool import_path(const std::string &path) {
  bool inside = false;
  for (auto &root : import_roots())
    if (path == root || path.compare(0, root.size() + 1, root + "/") == 0)
      inside = true;
  if (!inside || path.size() > 500)
    return false;
  for (unsigned char c : path)
    if (c < 32 || c == 127)
      return false;
  size_t start = 1;
  while (start <= path.size()) {
    auto end = path.find('/', start);
    auto segment =
        path.substr(start, end == std::string::npos ? end : end - start);
    if (segment.empty() || segment == "." || segment == ".." ||
        segment.find_first_of("\\\r\n") != std::string::npos)
      return false;
    struct stat st{};
    auto prefix = path.substr(0, end);
    if (lstat(prefix.c_str(), &st) == 0 && S_ISLNK(st.st_mode))
      return false;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return true;
}
bool import_list(const std::string &path,
                 const std::vector<std::string> &extensions,
                 std::vector<ImportEntry> &entries, std::string &error) {
  entries.clear();
  if (!import_path(path)) {
    error = "Pasta fora dos diretorios de importacao";
    return false;
  }
  if (path == import_roots()[0])
    mkdir(path.c_str(), 0700);
  DIR *dir = opendir(path.c_str());
  if (!dir) {
    error = "Pasta indisponivel no SD";
    return false;
  }
  while (auto *item = readdir(dir)) {
    std::string name = item->d_name;
    if (name == "." || name == "..")
      continue;
    auto full = path + "/" + name;
    struct stat st{};
    if (!import_path(full) || lstat(full.c_str(), &st) != 0)
      continue;
    bool directory = S_ISDIR(st.st_mode), accepted = directory;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (auto &ext : extensions)
      if (lower.size() >= ext.size() &&
          lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
        accepted = true;
    if (accepted && (directory || S_ISREG(st.st_mode)) && entries.size() < 128)
      entries.push_back({name, full, directory});
  }
  closedir(dir);
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    return a.directory != b.directory ? a.directory > b.directory
                                      : a.name < b.name;
  });
  return true;
}
bool import_read(const std::string &path, std::string &data,
                 std::string &error) {
  data.clear();
  struct stat st{};
  if (!import_path(path) || lstat(path.c_str(), &st) != 0 ||
      !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 8192) {
    error = "Arquivo invalido, fora da pasta de importacao ou maior que 8 KiB";
    return false;
  }
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    error = "Nao foi possivel abrir o arquivo";
    return false;
  }
  data.resize(st.st_size);
  bool ok = fread(data.data(), 1, data.size(), f) == data.size() && !ferror(f);
  fclose(f);
  if (!ok || data.find('\0') != std::string::npos) {
    std::fill(data.begin(), data.end(), 0);
    data.clear();
    error = "Arquivo de texto incompleto ou invalido";
    return false;
  }
  return true;
}
} // namespace tabremoto
