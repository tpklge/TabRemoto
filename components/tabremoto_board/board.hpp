#pragma once
#include "lvgl.h"
#include <cstdint>
namespace tabremoto {
struct BoardStatus { bool display=false, touch=false, keyboard=false; uint8_t keyboard_version=0; uint32_t flash_bytes=0; unsigned chip_revision=0; };
BoardStatus board_start();
void board_prepare_reboot();
}

namespace tabremoto { struct KeyStats { unsigned pressed,released,errors; }; KeyStats keyboard_stats(); }
namespace tabremoto { bool keyboard_event(uint8_t& event); }
