#pragma once
#include "lvgl.h"
#include <string>
namespace tabremoto {
struct RdpRequest;
struct RdpTouchStats{unsigned presses,moves,releases,cancels,taps,queued,dropped,last_ms,last_motion;};
RdpTouchStats rdp_ui_touch_stats();
bool rdp_ui_touch_test(unsigned,int,int,unsigned);
int rdp_ui_pointer_x();
int rdp_ui_pointer_y();
bool rdp_ui_sidebar_clear();
bool rdp_ui_icons_only();
bool rdp_ui_toast_visible();
bool rdp_ui_test_connect(RdpRequest *);
bool rdp_ui_test_accept(const std::string &);
bool rdp_ui_rendered();
bool rdp_ui_trust_visible();
void rdp_ui_start(lv_obj_t *, lv_group_t *, void (*)());
void rdp_ui_tick();
void rdp_ui_hide();
void rdp_ui_open();
void rdp_ui_profile(const std::string &);
bool rdp_ui_focused();
bool rdp_ui_submit_on_enter();
} // namespace tabremoto
