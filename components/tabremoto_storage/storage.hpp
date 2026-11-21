#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
namespace tabremoto {
constexpr auto sd_root="/sdcard/TabRemoto";
struct Resources { uint32_t background=0x101923,foreground=0xc7d5e2,accent=0x56d8b0; std::string title="Base do aparelho",hint="Digite aqui",retry="Tentar SD novamente"; std::vector<uint8_t> font, terminal_font; };
struct StorageStatus { bool mounted=false,writable=false,resources=false,busy=false; unsigned generation=0; std::string error; std::shared_ptr<Resources> assets; };
StorageStatus storage_start();
StorageStatus storage_snapshot();
void storage_retry();
bool storage_selftest(std::string& error);
bool storage_capture_begin();
void storage_capture_end();
}
