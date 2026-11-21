#pragma once
#include "lvgl.h"
#include <string>
namespace tabremoto {
void remote_ui_start(lv_obj_t *, lv_group_t *, void (*)());
void remote_ui_tick();
struct SshRequest;
bool remote_ui_test_connect(SshRequest *);
bool remote_ui_test_accept(const std::string &);
bool remote_ui_rendered_marker();
bool remote_ui_session_fullscreen();
bool remote_ui_trust_visible();
bool remote_ui_terminal_focused();
bool remote_ui_submit_on_enter();
std::string remote_ui_test_detail();
bool remote_ui_selftest();
void remote_ui_hide();
void remote_ui_files_open();
void remote_ui_profile(const std::string &id);
void remote_ui_set_font(const lv_font_t *fallback);
void remote_ui_open(bool network = false);
} // namespace tabremoto
