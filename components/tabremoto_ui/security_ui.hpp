#pragma once
#include "lvgl.h"
namespace tabremoto {
void security_ui_start(lv_obj_t* root,lv_group_t* group,void (*reset_focus)());
void security_ui_tick();
bool security_ui_selftest();
void security_ui_open();
bool security_ui_layout_preview(unsigned page);
}
