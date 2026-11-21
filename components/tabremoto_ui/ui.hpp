#pragma once
// Diagnostic pointer samples go through LVGL hit testing and input dispatch.
bool tabremoto_ui_pointer_sample(int x,int y,bool pressed);
#include "board.hpp"
#include "lvgl.h"
#include "storage.hpp"
#include <vector>
struct TabremotoPointerSample{int x,y;unsigned delay_ms;bool pressed;};
bool tabremoto_ui_pointer_gesture(const std::vector<TabremotoPointerSample>&);
bool tabremoto_ui_pointer_busy();
namespace tabremoto {
bool ui_panel_front(lv_obj_t *);
void ui_home();
void ui_sessions_open();
void ui_session_open(bool rdp, unsigned id);
void ui_session_new(bool rdp);
bool ui_sessions_cycle(bool backwards);
bool ui_click_label(const std::string &);
unsigned ui_action_result();
bool ui_inject_keys(const std::vector<uint8_t> &);
unsigned ui_max_tick_us();
unsigned ui_slow_ticks();
unsigned ui_button_layout_errors();
bool ui_start(const BoardStatus &, const StorageStatus &);
bool ui_snapshot();
void ui_terminal_probe();
std::string ui_test_detail();
unsigned ui_clicks();
unsigned ui_flushes();
bool ui_external_font();
void ui_test_request();
unsigned ui_test_result();
} // namespace tabremoto
