#pragma once
#include "lvgl.h"
#include <functional>
#include <string>
#include <vector>
namespace tabremoto {
void select_wifi_network(lv_obj_t*,lv_group_t*,bool,std::function<void(const std::string&,bool)>);
void select_rdp_resolution(lv_obj_t *, lv_group_t *,
                           std::function<void(unsigned, unsigned)>);
void select_network_route(lv_obj_t *,lv_group_t *,std::function<void(const std::string&,const std::string&)>);
void select_active_session(lv_obj_t *, lv_group_t *);
void show_notice(lv_obj_t *,lv_group_t *,const std::string &);
void confirm_action(lv_obj_t *, lv_group_t *, const std::string &,
                    std::function<void()>);
void selectors_start();
void selectors_tick();
void select_saved_host(lv_obj_t *parent, lv_group_t *group,
                       const std::string &protocol,
                       std::function<void(const std::string &)> selected);
void select_import_file(lv_obj_t *parent, lv_group_t *group,
                        const std::vector<std::string> &extensions,
                        std::function<void(const std::string &)> selected);
} // namespace tabremoto
