#include "rdp_ui.hpp"
#include "cJSON.h"
#include "rdp.hpp"
#include "sdkconfig.h"
#include "security.hpp"
#include "selectors.hpp"
#include "skin.hpp"
#include "touchpad.hpp"
#include "ui.hpp"
#include <atomic>
#include <cstring>
#include <functional>
#include <vector>
namespace tabremoto {
namespace {
lv_obj_t *panel, *form, *image, *status, *host, *user, *password, *domain,
    *port, *certificate, *fingerprint_label, *disconnect_button,
    *connect_button, *trust_button, *session_button, *resolution,
    *clipboard_button, *resize_button, *clipboard_modal, *clipboard_text,
    *clipboard_status, *reconnect_choice, *desktop, *rail, *right_button,
    *cursor, *cursor_border, *resize_modal, *toast, *left_button;
Touchpad touchpad;
std::atomic<unsigned> touch_presses{0},touch_moves{0},touch_releases{0},touch_cancels{0},touch_taps{0},touch_queued{0},touch_dropped{0},touch_ms{0},touch_motion{0};
uint32_t touch_started=0;int touch_start_x=0,touch_start_y=0,touch_max_motion=0;
unsigned touch_session=~0u;
uint32_t toast_until=0;
static const lv_point_precise_t cursor_points[]={{0,0},{0,19},{5,14},{10,23},{13,21},{8,12},{18,12},{0,0}};
unsigned clipboard_revision = 0, clipboard_received = 0;
std::array<unsigned,2> resize_errors_seen{};
lv_group_t *group;
lv_obj_t *previous;
std::vector<lv_obj_t *> hidden;
void (*reset_input)();
std::atomic<bool> requested{false};
std::atomic<RdpRequest *> ui_request{nullptr};
std::string profile,route_transport="auto",route_profile;
bool profile_pending = false;
unsigned security_revision = 0;
unsigned revision = ~0u;
lv_image_dsc_t descriptor{};
std::shared_ptr<const RdpFrame> retained;
void focus(lv_obj_t *o) {
  lv_group_add_obj(group, o);
  lv_obj_add_event_cb(
      o, [](lv_event_t *e) { lv_group_focus_obj(lv_event_get_target_obj(e)); },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *) {
        if (reset_input)
          reset_input();
      },
      LV_EVENT_FOCUSED, nullptr);
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w,
                 std::function<void()> fn) {
  auto *o = lv_button_create(parent);
  skin::control(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, 48);
  skin::button_content(o, text);
  focus(o);
  auto *action = new std::function<void()>(fn);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *e) {
        auto action =
            *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
        action();
      },
      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *e) {
        delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, action);
  return o;
}
lv_obj_t *field(const char *name, int x, int y, int w, bool secret = false) {
  auto *l = lv_label_create(form);
  lv_label_set_text(l, name);
  lv_obj_set_pos(l, x, y);
  auto *o = lv_textarea_create(form);
  skin::control(o);
  lv_obj_set_pos(o, x, y + 32);
  lv_obj_set_size(o, w, 50);
  lv_textarea_set_one_line(o, true);
  lv_textarea_set_max_length(o, 253);
  lv_textarea_set_password_mode(o, secret);
  if (secret)
    lv_textarea_set_password_show_time(o, 0);
  focus(o);
  return o;
}
void update_cursor() {
 auto state=rdp_status();
 int iw=lv_obj_get_width(image),ih=lv_obj_get_height(image);
 int x=lv_obj_get_x(image)+int64_t(touchpad.x)*iw/std::max(1u,state.width);
 int y=lv_obj_get_y(image)+int64_t(touchpad.y)*ih/std::max(1u,state.height);
 lv_obj_set_pos(cursor,x,y);lv_obj_set_pos(cursor_border,x,y);
 // Cursor objects are already the last children of desktop. Reordering
 // them here invalidates the entire parent every UI tick, even at rest.
}
void touch(Touchpad::Event event,int x,int y,uint32_t time) {
 if(rdp_status().phase!=RdpStatus::Ready)return;
 if(event==Touchpad::Press){++touch_presses;touch_started=time;touch_start_x=x;touch_start_y=y;touch_max_motion=0;}
 else if(event==Touchpad::Cancel)++touch_cancels;
 else {touch_max_motion=std::max(touch_max_motion,std::abs(x-touch_start_x)+std::abs(y-touch_start_y));
  if(event==Touchpad::Move)++touch_moves;
  else {++touch_releases;touch_ms=time-touch_started;touch_motion=touch_max_motion;}}
 touchpad.event_with_click(event,x,y,time,[](int px,int py,uint16_t flags){return rdp_pointer(px,py,flags);},[](int px,int py){
  ++touch_taps;bool ok=rdp_click(px,py);if(ok)++touch_queued;else ++touch_dropped;return ok;
 });
 update_cursor();
}
void notice(const std::string &text) {
 lv_label_set_text(toast,text.c_str());toast_until=lv_tick_get()+6000;
 lv_obj_remove_flag(toast,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(toast);
}
void close_resize() {
 if(!resize_modal)return;
 lv_obj_delete(resize_modal);resize_modal=nullptr;
 for(auto *control:{image,session_button,disconnect_button,clipboard_button,resize_button,right_button,left_button}){
  lv_obj_remove_state(control,LV_STATE_DISABLED);lv_group_add_obj(group,control);
 }
 lv_group_focus_obj(image);
}
void open_resize() {
 if(resize_modal)return;
 for(auto *control:{image,session_button,disconnect_button,clipboard_button,resize_button,right_button,left_button}){
  lv_obj_add_state(control,LV_STATE_DISABLED);lv_group_remove_obj(control);
 }
 resize_modal=lv_obj_create(panel);skin::surface(resize_modal);
 lv_obj_set_pos(resize_modal,78,132);lv_obj_set_size(resize_modal,340,292);
 lv_obj_remove_flag(resize_modal,LV_OBJ_FLAG_SCROLLABLE);
 auto *title=lv_label_create(resize_modal);lv_label_set_text(title,"Resolucao remota");lv_obj_set_pos(title,10,0);
 unsigned row=0;
 for(unsigned width:{1280u,960u,800u}){
  unsigned height=width*9/16;
  std::string label=std::to_string(width)+"x"+std::to_string(height);
  auto *item=button(resize_modal,label.c_str(),4,36+row++*54,304,[width,height]{
   close_resize();
   if(!rdp_resize(width,height)){
    auto state=rdp_status();resize_errors_seen[rdp_selected()]=state.resize_errors;notice(state.detail);
   }
  });
  if(rdp_status().width==width && rdp_status().height==height)lv_obj_add_state(item,LV_STATE_DISABLED);
 }
 auto *cancel=button(resize_modal,"Fechar",4,204,304,close_resize);lv_group_focus_obj(cancel);
}
lv_obj_t *rail_button(const char *action,const char *symbol,int y,std::function<void()> fn) {
 auto *b=button(rail,action,6,y,52,std::move(fn));lv_obj_set_height(b,52);lv_obj_set_style_pad_all(b,0,0);
 lv_obj_clean(b);auto *label=lv_label_create(b);lv_label_set_text(label,symbol);
 lv_obj_set_style_text_font(label,&lv_font_montserrat_32,0);lv_obj_add_flag(label,LV_OBJ_FLAG_USER_1);lv_obj_center(label);
 lv_obj_set_user_data(b,const_cast<char*>(action));return b;
}
void close_clipboard() {
  if (!clipboard_modal)
    return;
  auto *text = const_cast<char *>(lv_textarea_get_text(clipboard_text));
  volatile char *wipe = text;
  for (size_t i = 0, n = strlen(text); i < n; ++i)
    wipe[i] = 0;
  lv_obj_delete(clipboard_modal);
  clipboard_modal = clipboard_text = clipboard_status = nullptr;
  for (auto *control : {image, session_button, disconnect_button,
                        clipboard_button, resize_button, right_button}) {
    lv_obj_remove_state(control, LV_STATE_DISABLED);
    lv_group_add_obj(group, control);
  }
  lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);
  lv_group_focus_obj(image);
}
void open_clipboard() {
  if (clipboard_modal)
    return;
  for (auto *control : {image, session_button, disconnect_button,
                        clipboard_button, resize_button, right_button}) {
    lv_obj_add_state(control, LV_STATE_DISABLED);
    lv_group_remove_obj(control);
  }
  lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  clipboard_modal = lv_obj_create(panel);
  skin::surface(clipboard_modal);
  lv_obj_set_pos(clipboard_modal, 20, 60);
  lv_obj_set_size(clipboard_modal, 1240, 640);
  auto *label = lv_label_create(clipboard_modal);
  lv_label_set_text(
      label,
      "Clipboard de texto desta sessao | ate 4096 bytes | somente em memoria");
  lv_obj_set_pos(label, 12, 4);
  clipboard_text = lv_textarea_create(clipboard_modal);
  skin::control(clipboard_text);
  lv_obj_set_pos(clipboard_text, 12, 50);
  lv_obj_set_size(clipboard_text, 1180, 360);
  lv_textarea_set_max_length(clipboard_text, 4096);
  focus(clipboard_text);
  clipboard_status = lv_label_create(clipboard_modal);
  lv_obj_set_pos(clipboard_status, 12, 425);
  lv_obj_set_width(clipboard_status, 1170);
  button(clipboard_modal, "Enviar ao remoto", 12, 495, 370, [] {
    if (!rdp_clipboard_offer(lv_textarea_get_text(clipboard_text)))
      lv_label_set_text(clipboard_status,
                        "Clipboard nao disponivel ou texto maior que o limite");
  });
  button(clipboard_modal, "Receber do remoto", 410, 495, 370, [] {
    if (!rdp_clipboard_fetch())
      lv_label_set_text(clipboard_status,
                        "O servidor ainda nao anunciou texto disponivel");
  });
  button(clipboard_modal, "Fechar", 810, 495, 370, close_clipboard);
  clipboard_revision = ~0u;
  clipboard_received = rdp_clipboard_status().received;
  lv_group_focus_obj(clipboard_text);
}
void clear_password() {
  auto *t = const_cast<char *>(lv_textarea_get_text(password));
  volatile char *p = t;
  size_t n = strlen(t);
  for (size_t i = 0; i < n; i++)
    p[i] = 0;
  lv_textarea_set_text(password, "");
}
void close() {
  touch(Touchpad::Cancel,0,0,lv_tick_get());
  close_resize();
  close_clipboard();
  clear_password();
  if (retained) {
    lv_image_set_src(image, nullptr);
    lv_image_cache_drop(&descriptor);
    retained.reset();
    descriptor.data = nullptr;
  }
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  for (auto *o : hidden)
    if (lv_obj_is_valid(o))
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  hidden.clear();
  if (previous && lv_obj_is_valid(previous))
    lv_group_focus_obj(previous);
}
void open() {
  auto current=rdp_status();
  resize_errors_seen[rdp_selected()]=current.resize_errors;
  if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
    previous = lv_group_get_focused(group);
    auto *root = lv_obj_get_parent(panel);
    for (unsigned i = 0; i < lv_obj_get_child_count(root); i++) {
      auto *o = lv_obj_get_child(root, i);
      if (o != panel && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        hidden.push_back(o);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
    }
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel);
  }
  for (auto *field : {host, user, domain, port})
    lv_obj_remove_state(field, LV_STATE_DISABLED);
  if (!profile.empty()) {
    auto sec = security_status();
    auto *metadata =
        sec.metadata ? cJSON_Parse(sec.metadata->c_str()) : nullptr;
    auto value = [](cJSON *object, const char *key) {
      auto *v = cJSON_GetObjectItemCaseSensitive(object, key);
      return cJSON_IsString(v) ? v->valuestring : "";
    };
    auto *connections =
        cJSON_GetObjectItemCaseSensitive(metadata, "connections");
    for (auto *c = connections ? connections->child : nullptr; c; c = c->next)
      if (profile == value(c, "id")) {
        lv_textarea_set_text(host, value(c, "host"));
        auto *number = cJSON_GetObjectItemCaseSensitive(c, "port");
        if (cJSON_IsNumber(number))
          lv_textarea_set_text(port, std::to_string(number->valueint).c_str());
        auto id = std::string(value(c, "identity_id"));
        auto *identities =
            cJSON_GetObjectItemCaseSensitive(metadata, "identities");
        for (auto *i = identities ? identities->child : nullptr; i; i = i->next)
          if (id == value(i, "id")) {
            lv_textarea_set_text(user, value(i, "user"));
            lv_textarea_set_text(domain, value(i, "domain"));
          }
      }
    cJSON_Delete(metadata);
    for (auto *field : {host, user, domain, port})
      lv_obj_add_state(field, LV_STATE_DISABLED);
  }
  lv_group_focus_obj(profile.empty() ? host : password);
  revision = ~0u;
}
} // namespace
void rdp_ui_start(lv_obj_t *root, lv_group_t *g, void (*reset)()) {
  group = g;
  reset_input = reset;
  panel = lv_obj_create(root);
  skin::surface(panel, skin::bg, 0);
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_set_size(panel, 1280, 720);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  form = lv_obj_create(panel);
  skin::surface(form);
  lv_obj_set_pos(form, 24, 70);
  lv_obj_set_size(form, 1220, 540);
  host = field("Servidor RDP", 12, 20, 720);
  port = field("Porta", 760, 20, 180);
  lv_textarea_set_text(port, "3389");
  user = field("Usuario", 12, 130, 550);
  domain = field("Dominio (opcional)", 590, 130, 550);
  password = field("Senha", 12, 240, 1120, true);
  button(form,"Selecionar rota / VPN",12,330,600,[]{select_network_route(panel,group,[](const std::string &transport,const std::string &vpn){route_transport=transport;route_profile=vpn;lv_label_set_text(status,("Rota: "+transport+(vpn.empty()?"":" / "+vpn)).c_str());});});
  reconnect_choice = lv_checkbox_create(form);
  lv_checkbox_set_text(reconnect_choice, "Reconectar apos queda (ate 3)");
  lv_obj_set_pos(reconnect_choice, 650, 330);
  lv_obj_add_state(reconnect_choice, LV_STATE_CHECKED);
  focus(reconnect_choice);
  connect_button = button(form, "Conectar com NLA / TLS", 12, 390, 600, [] {
    if (!profile.empty()) {
      profile_pending = true;
      security_revision = security_status().revision;
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Rdp;
      auto size = lv_dropdown_get_selected(resolution);
      r->rdp_width = size == 1 ? 960 : size == 2 ? 800 : 1280;
      r->rdp_height = size == 1 ? 540 : size == 2 ? 450 : 720;
      r->rdp_reconnect = lv_obj_has_state(reconnect_choice, LV_STATE_CHECKED);
      r->id = profile;
      snprintf(r->password.data(), r->password.size(), "%s",
               lv_textarea_get_text(password));
      clear_password();
      if (!security_submit(r)) {
        delete r;
        lv_label_set_text(status, "Cofre ocupado ou bloqueado");
      }
      return;
    }
    auto *r = new RdpRequest;
    r->transport=route_transport;
    r->vpn_profile=route_profile;
    r->require_vpn=!route_profile.empty();
    r->host = lv_textarea_get_text(host);
    r->port = atoi(lv_textarea_get_text(port));
    r->user = lv_textarea_get_text(user);
    r->domain = lv_textarea_get_text(domain);
    r->reconnect = lv_obj_has_state(reconnect_choice, LV_STATE_CHECKED);
    auto size = lv_dropdown_get_selected(resolution);
    r->width = size == 1 ? 960 : size == 2 ? 800 : 1280;
    r->height = size == 1 ? 540 : size == 2 ? 450 : 720;
    snprintf(r->password.data(), r->password.size(), "%s",
             lv_textarea_get_text(password));
    clear_password();
    if (!rdp_connect(r)) {
      delete r;
      lv_label_set_text(status, "Campos invalidos ou sessao ocupada");
    }
  });
  button(form, "Sessoes", 760, 455, 340, [] { ui_sessions_open(); });
  button(form, "Selecionar host salvo", 12, 455, 600, [] {
    select_saved_host(panel, group, "rdp", [](const std::string &id) {
      profile = id;
      open();
    });
  });
  auto *resolution_label = lv_label_create(form);
  lv_label_set_text(resolution_label, "Resolucao");
  lv_obj_set_pos(resolution_label, 960, 20);
  resolution = lv_dropdown_create(form);
  skin::control(resolution);
  lv_obj_set_pos(resolution, 960, 52);
  lv_obj_set_size(resolution, 210, 50);
  lv_dropdown_set_options(resolution, "1280 x 720\n960 x 540\n800 x 450");
  focus(resolution);
  status = lv_label_create(panel);
  lv_obj_set_pos(status, 24, 625);
  lv_obj_set_width(status, 1210);
  lv_label_set_text(
      status,
      "Selecione um host salvo ou informe servidor, usuario e senha.\nConexao "
      "nativa com NLA / TLS e verificacao do certificado.");
  button(form, "Voltar", 780, 390, 300, [] { close(); });
  desktop=lv_obj_create(panel);skin::surface(desktop,0x101114,0);
  lv_obj_set_style_radius(desktop,0,0);lv_obj_set_style_border_width(desktop,0,0);
  lv_obj_set_pos(desktop,72,0);lv_obj_set_size(desktop,1208,720);lv_obj_remove_flag(desktop,LV_OBJ_FLAG_SCROLLABLE);
  image = lv_image_create(desktop);
  lv_obj_set_pos(image,0,20);lv_obj_set_size(image,1208,680);
  lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
  lv_obj_remove_flag(image,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(image,LV_OBJ_FLAG_PRESS_LOCK);focus(image);
  lv_obj_add_event_cb(image,[](lv_event_t *e){
   auto code=lv_event_get_code(e);Touchpad::Event event;
   if(code==LV_EVENT_PRESSED)event=Touchpad::Press;
   else if(code==LV_EVENT_PRESSING)event=Touchpad::Move;
   else if(code==LV_EVENT_RELEASED)event=Touchpad::Release;
   else if(code==LV_EVENT_PRESS_LOST)event=Touchpad::Cancel;
   else return;
   auto *input=lv_indev_active();if(!input)return;
   lv_point_t point;lv_indev_get_point(input,&point);touch(event,point.x,point.y,lv_tick_get());
  },LV_EVENT_ALL,nullptr);
  cursor_border=lv_line_create(desktop);cursor=lv_line_create(desktop);
  for(auto *o:{cursor_border,cursor}){
   lv_line_set_points(o,cursor_points,sizeof(cursor_points)/sizeof(cursor_points[0]));
   lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_style_line_width(cursor_border,5,0);lv_obj_set_style_line_color(cursor_border,lv_color_black(),0);
  lv_obj_set_style_line_width(cursor,2,0);lv_obj_set_style_line_color(cursor,lv_color_white(),0);
  rail=lv_obj_create(panel);skin::surface(rail,skin::panel,0);
  lv_obj_set_pos(rail,0,0);lv_obj_set_size(rail,68,720);lv_obj_set_style_radius(rail,0,0);
  lv_obj_remove_flag(rail,LV_OBJ_FLAG_SCROLLABLE);
  session_button=rail_button("Sessoes",LV_SYMBOL_LIST,12,[]{ui_sessions_open();});
  clipboard_button=rail_button("Clipboard",LV_SYMBOL_COPY,80,open_clipboard);
  resize_button=rail_button("Resolucao",LV_SYMBOL_IMAGE,148,open_resize);
  left_button=rail_button("Clique esquerdo",LV_SYMBOL_GPS,216,[]{
   if(!rdp_click(touchpad.x,touchpad.y))notice("Clique nao enviado; tente novamente");
   lv_group_focus_obj(image);
  });
  right_button=rail_button("Clique direito",LV_SYMBOL_BARS,284,[]{
   if(!rdp_click(touchpad.x,touchpad.y,true))notice("Clique nao enviado; tente novamente");
   lv_group_focus_obj(image);
  });
  disconnect_button=rail_button("Desconectar",LV_SYMBOL_POWER,652,[]{
   touch(Touchpad::Cancel,0,0,lv_tick_get());rdp_disconnect();
  });
  toast=lv_label_create(panel);skin::surface(toast,skin::panel,10);
  lv_obj_set_pos(toast,88,660);lv_obj_set_width(toast,1160);
  lv_obj_set_style_text_align(toast,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(rail,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(desktop,LV_OBJ_FLAG_HIDDEN);
  certificate = lv_obj_create(panel);
  skin::surface(certificate);
  lv_obj_set_pos(certificate, 40, 120);
  lv_obj_set_size(certificate, 1200, 470);
  fingerprint_label = lv_label_create(certificate);
  lv_obj_set_width(fingerprint_label, 1130);
  trust_button = button(certificate, "Confiar e salvar", 20, 340, 550,
                        [] { rdp_accept(rdp_status().fingerprint); });
  button(certificate, "Cancelar", 650, 340, 430, [] { rdp_disconnect(); });
  lv_obj_add_flag(certificate, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
void rdp_ui_hide() {
  if (panel && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    close();
}
void rdp_ui_open() {
  profile.clear();
  requested = true;
}
void rdp_ui_profile(const std::string &id) {
  profile = id;
  requested = true;
}
bool rdp_ui_focused() {
  return ui_panel_front(panel) && !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN) &&
         lv_group_get_focused(group) == image;
}
bool rdp_ui_test_connect(RdpRequest *request) {
  RdpRequest *empty = nullptr;
  return ui_request.compare_exchange_strong(empty, request);
}
bool rdp_ui_trust_visible() {
  return ui_panel_front(panel) &&
         !lv_obj_has_flag(certificate, LV_OBJ_FLAG_HIDDEN);
}
bool rdp_ui_test_accept(const std::string &pin) {
  if (!rdp_ui_trust_visible() || rdp_status().fingerprint != pin)
    return false;
  lv_obj_send_event(trust_button, LV_EVENT_CLICKED, nullptr);
  return true;
}
bool rdp_ui_touch_test(unsigned event,int x,int y,unsigned time) {
#if CONFIG_TABREMOTO_DIAGNOSTICS
 if(event>3 || !rdp_ui_focused())return false;
 touch(static_cast<Touchpad::Event>(event),x,y,time);return true;
#else
 (void)event;(void)x;(void)y;(void)time;return false;
#endif
}
int rdp_ui_pointer_x(){return touchpad.x;}
int rdp_ui_pointer_y(){return touchpad.y;}
RdpTouchStats rdp_ui_touch_stats(){return {touch_presses.load(),touch_moves.load(),touch_releases.load(),touch_cancels.load(),touch_taps.load(),touch_queued.load(),touch_dropped.load(),touch_ms.load(),touch_motion.load()};}
bool rdp_ui_sidebar_clear(){
 lv_area_t a,b;lv_obj_get_coords(rail,&a);lv_obj_get_coords(desktop,&b);
 return a.x2<b.x1 && !lv_obj_has_flag(rail,LV_OBJ_FLAG_HIDDEN);
}
bool rdp_ui_icons_only(){
 for(auto *b:{session_button,clipboard_button,resize_button,right_button,left_button,disconnect_button}){
  if(lv_obj_get_parent(b)!=rail || lv_obj_get_child_count(b)!=1)return false;
  auto *label=lv_obj_get_child(b,0);
  if(!lv_obj_check_type(label,&lv_label_class) || lv_obj_get_style_text_font(label,0)!=&lv_font_montserrat_32)return false;
 }
 return true;
}
bool rdp_ui_toast_visible(){return !lv_obj_has_flag(toast,LV_OBJ_FLAG_HIDDEN);}
bool rdp_ui_rendered() {
  return rdp_ui_focused() && retained &&
         descriptor.data == retained->pixels.get();
}
bool rdp_ui_submit_on_enter() {
  if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN) ||
      !lv_obj_has_flag(certificate, LV_OBJ_FLAG_HIDDEN))
    return false;
  auto *focused = lv_group_get_focused(group);
  if (focused != host && focused != user && focused != password &&
      focused != port && focused != domain)
    return false;
  lv_obj_send_event(connect_button, LV_EVENT_CLICKED, nullptr);
  return true;
}
void rdp_ui_tick() {
#if CONFIG_TABREMOTO_DIAGNOSTICS
  if (auto *raw = ui_request.exchange(nullptr)) {
    std::unique_ptr<RdpRequest> request(raw);
    profile.clear();
    route_transport=request->transport;route_profile=request->vpn_profile;
    open();
    lv_textarea_set_text(host, request->host.c_str());
    lv_textarea_set_text(user, request->user.c_str());
    lv_textarea_set_text(domain, request->domain.c_str());
    lv_textarea_set_text(port, std::to_string(request->port).c_str());
    lv_textarea_set_text(password, request->password.data());
    lv_dropdown_set_selected(resolution, request->width == 800   ? 2
                                         : request->width == 960 ? 1
                                                                 : 0);
    if (request->reconnect)
      lv_obj_add_state(reconnect_choice, LV_STATE_CHECKED);
    else
      lv_obj_remove_state(reconnect_choice, LV_STATE_CHECKED);
    lv_group_focus_obj(password);
    rdp_ui_submit_on_enter();
  }
#endif
  if (requested.exchange(false)) {
    open();
  }
  auto s = rdp_status();
  if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
    if(retained){lv_image_set_src(image,nullptr);lv_image_cache_drop(&descriptor);retained.reset();descriptor.data=nullptr;}
    return;
  }
  auto id=rdp_selected();
  if(toast_until && int32_t(lv_tick_get()-toast_until)>=0){lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);toast_until=0;}
  if(id<resize_errors_seen.size() && s.resize_errors>resize_errors_seen[id] && s.phase==RdpStatus::Ready && ui_panel_front(panel)) {
    resize_errors_seen[id]=s.resize_errors;notice(s.detail);
  }
  if (profile_pending) {
    auto sec = security_status();
    if (sec.revision != security_revision && !sec.busy) {
      profile_pending = false;
      if (s.phase != RdpStatus::Connecting && s.phase != RdpStatus::Verify &&
          s.phase != RdpStatus::Ready)
        lv_label_set_text(status, sec.message.c_str());
    }
  }
  if (clipboard_modal) {
    auto clip = rdp_clipboard_status();
    if (clip.revision != clipboard_revision) {
      clipboard_revision = clip.revision;
      lv_label_set_text(clipboard_status, clip.detail.c_str());
      if (clip.received != clipboard_received) {
        clipboard_received = clip.received;
        lv_textarea_set_text(clipboard_text, clip.text.c_str());
      }
    }
  }
  if (s.revision == revision)
    return;
  revision = s.revision;
  if (s.phase == RdpStatus::Connecting || s.phase == RdpStatus::Verify ||
      s.phase == RdpStatus::Closing)
    lv_obj_add_state(connect_button, LV_STATE_DISABLED);
  else
    lv_obj_remove_state(connect_button, LV_STATE_DISABLED);
  bool active = s.phase == RdpStatus::Ready,
       verify = s.phase == RdpStatus::Verify;
  if (active) {
    touchpad.geometry(s.width,s.height,touch_session!=id);touch_session=id;
    lv_obj_remove_flag(desktop,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(rail,LV_OBJ_FLAG_HIDDEN);
    int iw=1208,ih=int64_t(iw)*s.height/std::max(1u,s.width);
    if(ih>720){ih=720;iw=int64_t(ih)*s.width/std::max(1u,s.height);}
    lv_obj_set_pos(image,(1208-iw)/2,(720-ih)/2);lv_obj_set_size(image,iw,ih);update_cursor();
    lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(session_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(clipboard_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(resize_button, LV_OBJ_FLAG_HIDDEN);
    if (s.frame && s.frame != retained) {
      bool full=!retained || retained->width!=s.frame->width || retained->height!=s.frame->height || retained->sequence+1!=s.frame->sequence;
      bool geometry=!retained || retained->width!=s.frame->width || retained->height!=s.frame->height;
      retained = s.frame;
      descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
      descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
      descriptor.header.w = retained->width;
      descriptor.header.h = retained->height;
      descriptor.header.stride = retained->stride;
      descriptor.data_size = retained->bytes;
      descriptor.data = retained->pixels.get();
      lv_image_cache_drop(&descriptor);
      if(geometry)lv_image_set_src(image,&descriptor);
      else if(full)lv_obj_invalidate(image);
      else {
        lv_area_t bounds;lv_obj_get_coords(image,&bounds);auto d=retained->damage;
        int w=lv_obj_get_width(image),h=lv_obj_get_height(image);
        lv_area_t damage={int(bounds.x1+int64_t(d.x)*w/retained->width-2),int(bounds.y1+int64_t(d.y)*h/retained->height-2),
          int(bounds.x1+int64_t(d.x+d.w)*w/retained->width+2),int(bounds.y1+int64_t(d.y+d.h)*h/retained->height+2)};
        lv_obj_invalidate_area(image,&damage);
      }
    }
    if (!clipboard_modal && !resize_modal && lv_group_get_focused(group) != right_button && lv_group_get_focused(group) != left_button && lv_group_get_focused(group) != disconnect_button &&
        lv_group_get_focused(group) != session_button &&
        lv_group_get_focused(group) != clipboard_button &&
        lv_group_get_focused(group) != resize_button &&
        lv_group_get_focused(group) != image)
      lv_group_focus_obj(image);
  } else {
    touchpad.event(Touchpad::Cancel,0,0,lv_tick_get(),[](int,int,uint16_t){});
    close_resize();
    lv_obj_add_flag(desktop,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(rail,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);
    close_clipboard();
    lv_obj_add_flag(clipboard_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(resize_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    if (retained) {
      lv_image_set_src(image, nullptr);
      lv_image_cache_drop(&descriptor);
      retained.reset();
      descriptor.data = nullptr;
    }
    lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(session_button, LV_OBJ_FLAG_HIDDEN);
    if (s.phase == RdpStatus::Connecting) {
      lv_obj_remove_flag(rail,LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(form, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status, s.detail.c_str());
  }
  if (verify) {
    lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(certificate, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(
        fingerprint_label,
        ("Certificado RDP do servidor\nSHA-256: " + s.fingerprint +
         "\nConfira por fonte confiavel. Ao aceitar, o certificado sera salvo neste Tab5.")
            .c_str());
    lv_group_focus_obj(lv_obj_get_child(certificate, 1));
  } else
    lv_obj_add_flag(certificate, LV_OBJ_FLAG_HIDDEN);
}
} // namespace tabremoto
