#include "ui.hpp"
#include "bsp/m5stack_tab5.h"
#include "cJSON.h"
#include "composition.hpp"
#include "esp_timer.h"
#include "keymap.hpp"
#include "network.hpp"
#include "rdp.hpp"
#include "rdp_ui.hpp"
#include "remote_ui.hpp"
#include "sdkconfig.h"
#include "security.hpp"
#include "security_ui.hpp"
#include "selectors.hpp"
#include "skin.hpp"
#include "ssh.hpp"
#include <atomic>
#include <functional>
#include <cstdio>
#include <utility>
#include <memory>
#include <new>
namespace tabremoto {
static lv_obj_t *home, *diagnostics, *home_status, *home_heading, *home_default;
#if CONFIG_TABREMOTO_DIAGNOSTICS
static lv_obj_t *diagnostic_name;
#endif
static lv_obj_t *favorites_area;
static std::atomic<bool> terminal_probe{false};
static std::string test_detail;
std::string ui_test_detail() { return test_detail; }
static std::atomic<unsigned> clicks{0}, flushes{0};
static std::atomic<unsigned> max_tick_us{0}, slow_ticks{0};
unsigned ui_max_tick_us() { return max_tick_us; }
unsigned ui_slow_ticks() { return slow_ticks; }
#if CONFIG_TABREMOTO_DIAGNOSTICS
static std::atomic<std::vector<uint8_t> *> injected_keys{nullptr};
static std::atomic<std::string *> injected_click{nullptr};
static std::atomic<unsigned> action_result{0};
static std::atomic<std::vector<TabremotoPointerSample>*> pointer_requested{nullptr};
static std::atomic<bool> pointer_inflight{false};
static std::vector<TabremotoPointerSample> *pointer_playing=nullptr;
static unsigned pointer_index=0;static uint32_t pointer_due=0;
#endif
unsigned ui_action_result() {
#if CONFIG_TABREMOTO_DIAGNOSTICS
  return action_result;
#else
  return 0;
#endif
}
bool ui_click_label(const std::string &label) {
#if CONFIG_TABREMOTO_DIAGNOSTICS
  if (label.empty() || label.size() > 256)
    return false;
  auto *text = new std::string(label);
  std::string *empty = nullptr;
  if (!injected_click.compare_exchange_strong(empty, text)) {
    delete text;
    return false;
  }
  action_result = 1;
  return true;
#else
  (void)label;
  return false;
#endif
}
#if CONFIG_TABREMOTO_DIAGNOSTICS
static lv_obj_t *find_button(lv_obj_t *object, const std::string &label) {
  if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
    return nullptr;
  if (lv_obj_check_type(object, &lv_button_class) && lv_obj_get_user_data(object) &&
      label==static_cast<const char*>(lv_obj_get_user_data(object)))return object;
  if (lv_obj_check_type(object, &lv_button_class))
    for (unsigned i = 0; i < lv_obj_get_child_count(object); i++) {
      auto *child = lv_obj_get_child(object, i);
      if (lv_obj_check_type(child, &lv_label_class) &&
          label == lv_label_get_text(child))
        return object;
    }
  for (unsigned i = 0; i < lv_obj_get_child_count(object); i++)
    if (auto *found = find_button(lv_obj_get_child(object, i), label))
      return found;
  return nullptr;
}
#endif
bool ui_inject_keys(const std::vector<uint8_t> &keys) {
#if CONFIG_TABREMOTO_DIAGNOSTICS
  if (keys.empty() || keys.size() > 256)
    return false;
  auto *value = new std::vector<uint8_t>(keys);
  std::vector<uint8_t> *empty = nullptr;
  if (!injected_keys.compare_exchange_strong(empty, value)) {
    delete value;
    return false;
  }
  return true;
#else
  (void)keys;
  return false;
#endif
}
struct TickMeasure {
  int64_t start = esp_timer_get_time();
  ~TickMeasure() {
    unsigned elapsed = esp_timer_get_time() - start;
    if (elapsed > 50000)
      ++slow_ticks;
    unsigned before = max_tick_us;
    while (before < elapsed &&
           !max_tick_us.compare_exchange_weak(before, elapsed)) {
    }
  }
};
static KeyRepeat input;
static Composition composition;
static lv_group_t *focus;
static lv_obj_t *root, *title, *state_label, *hint, *retry_caption;
unsigned ui_button_layout_errors() {
 unsigned errors=0;lv_obj_update_layout(root);
 std::function<void(lv_obj_t*)> inspect=[&](lv_obj_t *o){
  if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;
  if(lv_obj_check_type(o,&lv_button_class)){
   lv_area_t outer;lv_obj_get_coords(o,&outer);
   for(unsigned i=0;i<lv_obj_get_child_count(o);++i){
    auto *child=lv_obj_get_child(o,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
    lv_area_t area;lv_obj_get_coords(child,&area);
    if(area.x1<outer.x1 || area.x2>outer.x2 || area.y1<outer.y1 || area.y2>outer.y2)++errors;
    if(i==0 && lv_obj_get_child_count(o)>1 && !lv_obj_has_flag(o,LV_OBJ_FLAG_USER_2) && lv_obj_get_style_text_align(child,0)!=LV_TEXT_ALIGN_CENTER)++errors;
   }
  }
  for(unsigned i=0;i<lv_obj_get_child_count(o);++i)inspect(lv_obj_get_child(o,i));
 };inspect(root);return errors;
}
static lv_font_t *external_font = nullptr;
static lv_font_t *terminal_font = nullptr;
static std::shared_ptr<Resources> retained;
static unsigned generation = 0;
static std::atomic<bool> font_ready{false}, test_requested{false};
static std::atomic<unsigned> test_result{0};
bool ui_external_font() { return font_ready.load(); }
void ui_test_request() {
  test_result = 1;
  test_requested = true;
}
unsigned ui_test_result() { return test_result.load(); }
static void refresh_hint() {
  if (!hint)
    return;
  if (composition.pending)
    lv_label_set_text_fmt(
        hint, "Acento pendente: %c | Espaco: literal | Esc: cancelar",
        composition.pending);
  else
    lv_label_set_text(
        hint, retained
                  ? retained->hint.c_str()
                  : "Aspas + vogal: agudo | Aa + aspas, soltar, vogal: trema");
}
static void reset_input() {
  if (rdp_status().phase == RdpStatus::Ready) {
    for (uint16_t key : {0x1d, 0x38, 0x2a, 0x36})
      rdp_key(key, false);
  }
  input.event(255, lv_tick_get());
  composition.reset();
  refresh_hint();
}
bool ui_panel_front(lv_obj_t *panel) {
  if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return false;
  auto *parent = lv_obj_get_parent(panel);
  if (!parent)
    return false;
  for (unsigned i = lv_obj_get_index(panel) + 1;
       i < lv_obj_get_child_count(parent); ++i)
    if (!lv_obj_has_flag(lv_obj_get_child(parent, i), LV_OBJ_FLAG_HIDDEN))
      return false;
  return true;
}
void ui_home() {
  // Unwind both overlay histories: closing one can restore the other.
  reset_input();
  for (unsigned i = 0; i < 2; ++i) {
    remote_ui_hide();
    rdp_ui_hide();
  }
  if (home) {
    lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(home);
    lv_group_focus_obj(home_default);
  }
}
void ui_sessions_open() {
  reset_input();
  select_active_session(root, focus);
}
void ui_session_open(bool remote, unsigned id) {
  reset_input();
  ui_home();
  if (remote) {
    if (rdp_select(id))
      rdp_ui_open();
  } else if (ssh_select(id))
    remote_ui_open();
}
void ui_session_new(bool remote) {
  reset_input();
  bool ok = remote ? rdp_new_session() : ssh_new_session();
  if (!ok) {
    confirm_action(root, focus,
                   "Limite de sessoes ou memoria insuficiente. Feche uma "
                   "sessao antes de abrir outra.",
                   [] {});
    return;
  }
  ui_home();
  if (remote)
    rdp_ui_open();
  else
    remote_ui_open();
}
bool ui_sessions_cycle(bool backwards) {
  std::vector<std::pair<bool, unsigned>> tabs;
  for (const auto &tab : ssh_sessions())
    if (tab.status.phase >= SshStatus::Connecting &&
        tab.status.phase <= SshStatus::Ready)
      tabs.push_back({false, tab.id});
  for (const auto &tab : rdp_sessions())
    if (tab.status.phase >= RdpStatus::Connecting &&
        tab.status.phase <= RdpStatus::Ready)
      tabs.push_back({true, tab.id});
  if (tabs.size() < 2)
    return false;
  auto current = std::make_pair(
      rdp_ui_focused(), rdp_ui_focused() ? rdp_selected() : ssh_selected());
  size_t index = 0;
  for (size_t i = 0; i < tabs.size(); ++i)
    if (tabs[i] == current)
      index = i;
  index = (index + tabs.size() + (backwards ? -1 : 1)) % tabs.size();
  ui_session_open(tabs[index].first, tabs[index].second);
  return true;
}
static lv_obj_t *label(const char *text, int x, int y) {
  auto *o = lv_label_create(root);
  lv_label_set_text(o, text);
  lv_obj_set_pos(o, x, y);
  return o;
}
static void set_font(lv_obj_t *obj, const lv_font_t *font) {
  if(!lv_obj_has_flag(obj,LV_OBJ_FLAG_USER_1)) {
    lv_obj_set_style_text_font(obj, font, 0);
    auto *parent=lv_obj_get_parent(obj);
    if(lv_obj_check_type(obj,&lv_label_class) && parent &&
       lv_obj_check_type(parent,&lv_button_class) && lv_obj_get_child(parent,0)==obj &&
       lv_obj_get_style_height(parent,LV_PART_MAIN)<64)
      lv_obj_set_height(obj,font->line_height);
  }
  for (unsigned i = 0; i < lv_obj_get_child_count(obj); i++)
    set_font(lv_obj_get_child(obj, i), font);
}
static void refresh_storage() {
  auto s = storage_snapshot();
  if (s.generation == generation)
    return;
  generation = s.generation;
  char text[320];
  snprintf(text, sizeof(text), "SD: %s | Escrita: %s | Recursos: %s%s\n%s",
           s.mounted ? "montado" : "ausente",
           s.writable ? "OK" : "indisponivel",
           s.resources ? "validos" : "diagnostico interno",
           s.busy ? " | verificando..." : "", s.error.c_str());
  lv_label_set_text(state_label, text);
  if (s.resources && s.assets && s.assets != retained) {
    auto *font = lv_binfont_create_from_buffer(s.assets->font.data(),
                                               s.assets->font.size());
    auto *terminal = lv_binfont_create_from_buffer(
        s.assets->terminal_font.data(), s.assets->terminal_font.size());
    if (font && terminal) {
      auto *old_terminal = terminal_font;
      terminal_font = terminal;
      auto *previous = external_font;
      auto old = retained;
      external_font = font;
      retained = s.assets;
      set_font(root, font);
      if (home_heading)
        lv_obj_set_style_text_font(home_heading, &lv_font_montserrat_32, 0);
      remote_ui_set_font(terminal_font);
      lv_obj_set_style_bg_color(root, lv_color_hex(skin::bg), 0);
      lv_obj_set_style_text_color(root, lv_color_hex(retained->foreground), 0);
      lv_obj_set_style_text_color(title, lv_color_hex(retained->accent), 0);
      for (unsigned i = 0; i < lv_obj_get_child_count(root); i++)
        lv_obj_set_style_outline_color(lv_obj_get_child(root, i),
                                       lv_color_hex(retained->accent),
                                       LV_STATE_FOCUSED);
      font_ready = true;
      lv_label_set_text(title, retained->title.c_str());
      if (hint)
        lv_label_set_text(hint, retained->hint.c_str());
      lv_label_set_text(retry_caption, retained->retry.c_str());
      if (previous)
        lv_binfont_destroy(previous);
      if (old_terminal)
        lv_binfont_destroy(old_terminal);
    } else {
      if (font)
        lv_binfont_destroy(font);
      if (terminal)
        lv_binfont_destroy(terminal);
      font_ready = false;
    }
  }
}
static void focusable(lv_obj_t *obj) {
  lv_group_add_obj(focus, obj);
  lv_obj_set_style_outline_width(obj, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(obj, lv_color_hex(skin::accent),
                                 LV_STATE_FOCUSED);
  lv_obj_add_event_cb(
      obj, [](lv_event_t *e) { reset_input(); }, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(
      obj,
      [](lv_event_t *e) { lv_group_focus_obj(lv_event_get_target_obj(e)); },
      LV_EVENT_CLICKED, nullptr);
}
static void refresh_favorites() {
  static unsigned previous = ~0u;
  auto state = security_status();
  if (state.revision == previous || !favorites_area ||
      lv_obj_has_flag(home, LV_OBJ_FLAG_HIDDEN))
    return;
  previous = state.revision;
  lv_obj_clean(favorites_area);
  auto *caption = lv_label_create(favorites_area);
  lv_obj_set_pos(caption, 0, 0);
  lv_label_set_text(caption,
                    state.unlocked
                        ? "Favoritos"
                        : "Desbloqueie o cofre para abrir seus favoritos");
  auto *metadata =
      state.metadata ? cJSON_Parse(state.metadata->c_str()) : nullptr;
  auto *list = cJSON_GetObjectItemCaseSensitive(metadata, "connections");
  unsigned count = 0;
  auto value = [](cJSON *object, const char *key) {
    auto *v = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(v) ? v->valuestring : "";
  };
  for (auto *item = list ? list->child : nullptr; item && count < 4;
       item = item->next) {
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "favorite")))
      continue;
    auto *entry = new std::pair<std::string, std::string>(
        value(item, "id"), value(item, "protocol"));
    auto *button = lv_button_create(favorites_area);
    skin::control(button);
    lv_obj_set_pos(button, count * 254, 28);
    lv_obj_set_size(button, 242, 44);
    focusable(button);
    skin::button_content(button, value(item, "label"));
    lv_obj_add_event_cb(
        button,
        [](lv_event_t *e) {
          auto *target = static_cast<std::pair<std::string, std::string> *>(
              lv_event_get_user_data(e));
          if (target->second == "rdp")
            rdp_ui_profile(target->first);
          else
            remote_ui_profile(target->first);
        },
        LV_EVENT_CLICKED, entry);
    lv_obj_add_event_cb(
        button,
        [](lv_event_t *e) {
          delete static_cast<std::pair<std::string, std::string> *>(
              lv_event_get_user_data(e));
        },
        LV_EVENT_DELETE, entry);
    ++count;
  }
  cJSON_Delete(metadata);
  if (state.unlocked && !count)
    lv_label_set_text(caption, "Marque uma conexao como favorita no cofre");
}
static void apply(int ch) {
  auto *obj = lv_group_get_focused(focus);
  if (!obj || !ch)
    return;
  security_activity();
  if (ch == 9 && input.map.ctrl && ui_sessions_cycle(input.map.shift))
    return;
  if (rdp_ui_focused()) {
    uint16_t scan = ch >= F1 && ch <= F10 ? 0x3b + ch - F1
                    : ch == F11           ? 0x57
                    : ch == F12           ? 0x58
                    : ch == Left          ? 0x14b
                    : ch == Right         ? 0x14d
                    : ch == Up            ? 0x148
                    : ch == Down          ? 0x150
                    : ch == Home          ? 0x147
                    : ch == End           ? 0x14f
                    : ch == Delete        ? 0x153
                    : ch == 8             ? 0x0e
                    : ch == 9             ? 0x0f
                    : ch == 10            ? 0x1c
                    : ch == 27            ? 0x01
                                          : 0;
    if (scan) {
      bool function = ch >= F1 && ch <= F12;
      if (function)
        rdp_key(0x2a, false);
      rdp_key(scan, true);
      rdp_key(scan, false);
      if (function && input.map.shift)
        rdp_key(0x2a, true);
      return;
    }
    if (ch >= 1 && ch <= 26) {
      static const uint8_t scans[] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22,
                                      0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31,
                                      0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16,
                                      0x2f, 0x11, 0x2d, 0x15, 0x2c};
      rdp_key(scans[ch - 1], true);
      rdp_key(scans[ch - 1], false);
      return;
    }
    auto value = composition.feed(ch, input.map.alt);
    uint32_t point = 0;
    unsigned remaining = 0;
    for (unsigned char byte : value) {
      if (byte < 128) {
        point = byte;
        remaining = 0;
      } else if (byte >= 0xc0) {
        remaining = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
        point = byte & (remaining == 1 ? 31 : remaining == 2 ? 15 : 7);
        continue;
      } else {
        point = (point << 6) | (byte & 63);
        if (--remaining)
          continue;
      }
      if (point <= 65535) {
        rdp_key(point, true, true);
        rdp_key(point, false, true);
      }
    }
    return;
  }
  if (remote_ui_terminal_focused() && !(ch == 9 && input.map.ctrl)) {
    TerminalKey key =
        ch >= F1 && ch <= F12
            ? static_cast<TerminalKey>(int(TerminalKey::F1) + ch - F1)
        : ch == Left   ? TerminalKey::Left
        : ch == Right  ? TerminalKey::Right
        : ch == Up     ? TerminalKey::Up
        : ch == Down   ? TerminalKey::Down
        : ch == Home   ? TerminalKey::Home
        : ch == End    ? TerminalKey::End
        : ch == Delete ? TerminalKey::Delete
        : ch == 9      ? TerminalKey::Tab
                       : TerminalKey::None;
    if (key != TerminalKey::None) {
      unsigned mods = (input.map.alt ? 2 : 0) | (input.map.ctrl ? 4 : 0);
      if (input.map.shift && !(ch >= F1 && ch <= F12))
        mods |= 1;
      ssh_key(key, mods);
      return;
    }
    if (input.map.alt && ch >= 32 && ch < 127) {
      composition.reset();
      ssh_input(std::string("\x1b") + char(ch));
      return;
    }
    std::string out;
    if (ch == 27) {
      composition.reset();
      out = "\x1b";
    } else if (ch == Left)
      out = "\x1b[D";
    else if (ch == Right)
      out = "\x1b[C";
    else if (ch == Up)
      out = "\x1b[A";
    else if (ch == Down)
      out = "\x1b[B";
    else if (ch == Home)
      out = "\x1b[H";
    else if (ch == End)
      out = "\x1b[F";
    else if (ch == Delete)
      out = "\x1b[3~";
    else if (ch == 9)
      out = "\t";
    else if (ch == 10)
      out = "\r";
    else if (ch == 8) {
      if (composition.pending)
        composition.reset();
      else
        out = "\x7f";
    } else if (ch > 0 && ch < 32)
      out = char(ch);
    else
      out = composition.feed(ch, input.map.alt);
    if (!out.empty())
      ssh_input(out);
    return;
  }
  if (ch >= F1 && ch <= F12)
    return;
  if (ch == 10 && (remote_ui_submit_on_enter() || rdp_ui_submit_on_enter()))
    return;
  if (ch == 9) {
    bool backwards = input.map.shift;
    reset_input();
    if (backwards)
      lv_group_focus_prev(focus);
    else
      lv_group_focus_next(focus);
    return;
  }
  if (ch == 27) {
    reset_input();
    return;
  }
  if (!lv_obj_check_type(obj, &lv_textarea_class)) {
    if (ch == 10 || ch == ' ')
      lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    return;
  }
  if (ch == Left)
    lv_textarea_cursor_left(obj);
  else if (ch == Right)
    lv_textarea_cursor_right(obj);
  else if (ch == Up)
    lv_textarea_cursor_up(obj);
  else if (ch == Down)
    lv_textarea_cursor_down(obj);
  else if (ch == Home)
    lv_textarea_set_cursor_pos(obj, 0);
  else if (ch == End)
    lv_textarea_set_cursor_pos(obj, LV_TEXTAREA_CURSOR_LAST);
  else if (ch == Delete)
    lv_textarea_delete_char_forward(obj);
  else if (ch == 8) {
    if (composition.pending)
      composition.reset();
    else
      lv_textarea_delete_char(obj);
  } else if (ch == 10)
    lv_textarea_add_char(obj, ch);
  else {
    auto text = composition.feed(ch, input.map.alt);
    if (!text.empty())
      lv_textarea_add_text(obj, text.c_str());
  }
  refresh_hint();
}
#if CONFIG_TABREMOTO_DIAGNOSTICS
static void selftest() {
  auto *original = lv_group_get_focused(focus);
  lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
  lv_group_focus_obj(diagnostic_name);
  auto *field = diagnostic_name;
  // Locate an editable object without depending on the operator's current
  // focus.
  for (unsigned i = 0; i < 4 && !lv_obj_check_type(field, &lv_textarea_class);
       i++) {
    lv_group_focus_next(focus);
    field = lv_group_get_focused(focus);
  }
  if (!lv_obj_check_type(field, &lv_textarea_class)) {
    test_result = 3;
    return;
  }
  std::string saved = lv_textarea_get_text(field);
  int cursor = lv_textarea_get_cursor_pos(field);
  reset_input();
  lv_textarea_set_text(field, "");
  auto send = [](uint8_t raw) { apply(input.event(raw, lv_tick_get())); };
  send(0xac);
  send(0x2c);
  send(0xb2);
  send(0x32);
  composition.feed('~', true);
  apply('o');
  apply('c');
  bool ok = std::string(lv_textarea_get_text(field)) == "áõc";
  apply(8);
  ok = ok && std::string(lv_textarea_get_text(field)) == "áõ";
  apply(Home);
  apply(Delete);
  ok = ok && std::string(lv_textarea_get_text(field)) == "õ";
  lv_textarea_set_text(field, "");
  // Physical Aa + quote => double quote; release Aa before the lower-case
  // vowel.
  send(0xb1);
  send(0xac);
  send(0x2c);
  send(0x31);
  send(0xa7);
  send(0x27);
  send(0xb1);
  send(0xac);
  send(0x2c);
  send(0xa7);
  send(0x27);
  send(0x31);
  ok = ok && std::string(lv_textarea_get_text(field)) == "üÜ";
  apply(8);
  ok = ok && std::string(lv_textarea_get_text(field)) == "ü";
  apply(9);
  ok = ok && lv_group_get_focused(focus) != field;
  input.map.shift = true;
  apply(9);
  ok = ok && lv_group_get_focused(focus) == field;
  lv_textarea_set_text(field, saved.c_str());
  lv_textarea_set_cursor_pos(field, cursor);
  lv_group_focus_obj(original);
  reset_input();
  lv_obj_add_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
  lv_group_focus_obj(original);
  bool base = ok;
  bool sec = security_ui_selftest();
  bool remote = remote_ui_selftest();
  test_detail =
      "base=" + std::to_string(base) + " cofre=" + std::to_string(sec) +
      " remoto=" + std::to_string(remote) + " " + remote_ui_test_detail();
  test_result = base && sec && remote ? 2 : 3;
}
#endif
bool ui_start(const BoardStatus &board, const StorageStatus &) {
  if (!board.display || !lvgl_port_lock(2000))
    return false;
  root = lv_screen_active();
  skin::surface(root, skin::bg, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(root, &lv_font_montserrat_20, 0);
  lv_display_add_event_cb(
      lv_display_get_default(), [](lv_event_t *) { ++flushes; },
      LV_EVENT_FLUSH_FINISH, nullptr);
  focus = lv_group_create();
  home = lv_obj_create(root);
  lv_obj_set_size(home, 1280, 720);
  skin::surface(home, skin::bg, 0);
  lv_obj_set_style_border_width(home, 0, 0);
  lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
  diagnostics = lv_obj_create(root);
  lv_obj_set_size(diagnostics, 1280, 720);
  skin::surface(diagnostics, skin::bg, 0);
  lv_obj_set_style_border_width(diagnostics, 0, 0);
  auto *screen = root;
  root = diagnostics;
  label("TabRemoto", 32, 20);
  title = label("Base do aparelho / diagnostico", 32, 58);
  state_label = label("Verificando SD...", 32, 105);
  lv_obj_set_width(state_label, 1200);
#if CONFIG_TABREMOTO_DIAGNOSTICS
  label("Nome / teste de foco", 32, 180);

  auto *name = lv_textarea_create(diagnostics);
  lv_obj_set_pos(name, 32, 210);
  lv_obj_set_size(name, 1200, 54);
  lv_textarea_set_one_line(name, true);
  lv_textarea_set_max_length(name, 80);
  focusable(name);
  label("Texto de teste (somente RAM)", 32, 280);
  auto *field = lv_textarea_create(diagnostics);
  lv_obj_set_pos(field, 32, 310);
  lv_obj_set_size(field, 1200, 125);
  lv_textarea_set_max_length(field, 256);
  focusable(field);
  hint = label("Aa + aspas, soltar, depois vogal: trema", 32, 450);
  lv_obj_set_width(hint, 1200);
  auto *button = lv_button_create(diagnostics);
  lv_obj_set_pos(button, 32, 500);
  lv_obj_set_size(button, 400, 60);
  focusable(button);
  auto *caption = skin::button_content(button,"Testar toque");
  lv_obj_add_event_cb(
      button,
      [](lv_event_t *e) {
        lv_label_set_text_fmt((lv_obj_t *)lv_event_get_user_data(e),
                              "Toques: %u", ++clicks);
      },
      LV_EVENT_CLICKED, caption);
#endif
  auto *retry = lv_button_create(diagnostics);
  lv_obj_set_pos(retry, 480, 500);
  lv_obj_set_size(retry, 500, 60);
  focusable(retry);
  retry_caption = skin::button_content(retry,"Tentar SD novamente");
  lv_obj_add_event_cb(
      retry, [](lv_event_t *) { storage_retry(); }, LV_EVENT_CLICKED, nullptr);
  label("Tab / Shift+Tab: foco | Enter: ativar botao | Aa: Shift", 32, 590);
  label("Recursos do aplicativo: /TabRemoto no cartao SD", 32, 635);

#if CONFIG_TABREMOTO_DIAGNOSTICS
  diagnostic_name = name;
#endif
  root = screen;
  lv_obj_add_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
  auto make_button = [&](lv_obj_t *parent, const char *text, int x, int y,
                         int w, int h, lv_event_cb_t cb) {
    auto *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    skin::control(b);
    focusable(b);
    skin::button_content(b, text);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
  };
  make_button(diagnostics, "Voltar", 1020, 20, 210, 46, [](lv_event_t *) {
    lv_obj_add_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(home_default);
  });
  auto *rail = lv_obj_create(home);
  lv_obj_set_size(rail, 128, 720);
  skin::surface(rail, 0x191C21, 8);
  lv_obj_set_style_radius(rail, 0, 0);
  auto *brand = lv_label_create(rail);
  lv_label_set_text(brand, "TR");
  lv_obj_set_pos(brand, 25, 28);
  lv_obj_set_style_text_font(brand,&lv_font_montserrat_24,0);lv_obj_add_flag(brand,LV_OBJ_FLAG_USER_1);
  lv_obj_set_style_text_color(brand, lv_color_hex(skin::accent), 0);
  make_button(rail, "SSH", 8, 130, 96, 76,
              [](lv_event_t *) { remote_ui_open(); });
  make_button(rail, "COFRE", 8, 226, 96, 76,
              [](lv_event_t *) { security_ui_open(); });
  make_button(rail, "Arquivos", 8, 514, 96, 76,
              [](lv_event_t *) { remote_ui_files_open(); });
  make_button(rail, "RDP", 8, 418, 96, 76, [](lv_event_t *) { rdp_ui_open(); });
  make_button(rail, "REDE", 8, 322, 96, 76,
              [](lv_event_t *) { remote_ui_open(true); });
#if CONFIG_TABREMOTO_DIAGNOSTICS
  make_button(rail, "TESTES", 8, 620, 96, 76, [](lv_event_t *) {
    lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(diagnostic_name);
  });
#endif
#if !CONFIG_TABREMOTO_DIAGNOSTICS
  make_button(rail, "SD", 8, 620, 96, 76, [](lv_event_t *) {
    lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(diagnostics, LV_OBJ_FLAG_HIDDEN);
  });
#endif
  make_button(home, "Sessoes", 990, 30, 230, 60,
              [](lv_event_t *) { ui_sessions_open(); });
  auto *heading = lv_label_create(home);
  home_heading = heading;
  lv_label_set_text(heading, "TabRemoto");
  lv_obj_set_pos(heading, 176, 42);
  lv_obj_set_style_text_font(heading, &lv_font_montserrat_32, 0);
  auto *sub = lv_label_create(home);
  lv_label_set_text(sub, "Seus servidores, ao alcance do teclado.");
  lv_obj_set_pos(sub, 178, 96);
  lv_obj_set_style_text_color(sub, lv_color_hex(skin::muted), 0);
  auto *connection_strip=lv_obj_create(home);skin::surface(connection_strip,skin::panel,0);
  lv_obj_set_pos(connection_strip,176,632);lv_obj_set_size(connection_strip,1045,56);lv_obj_remove_flag(connection_strip,LV_OBJ_FLAG_SCROLLABLE);
  home_status=lv_label_create(connection_strip);lv_obj_set_pos(home_status,18,15);lv_obj_set_width(home_status,1005);
  auto feature_card=[&](const char *name,const char *description,const char *symbol,int x,int y,int width,lv_event_cb_t action){
    auto *card=lv_button_create(home);skin::surface(card,0x20242B,0);
    lv_obj_set_pos(card,x,y);lv_obj_set_size(card,width,145);lv_obj_set_style_radius(card,14,0);
    lv_obj_add_flag(card,LV_OBJ_FLAG_USER_2);focusable(card);lv_obj_add_event_cb(card,action,LV_EVENT_CLICKED,nullptr);
    auto *title=lv_label_create(card);lv_label_set_text(title,name);lv_obj_set_pos(title,94,21);lv_obj_set_width(title,width-122);
    lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);lv_obj_add_flag(title,LV_OBJ_FLAG_USER_1);
    auto *description_label=lv_label_create(card);lv_label_set_text(description_label,description);lv_obj_set_pos(description_label,94,63);
    lv_obj_set_size(description_label,width-124,58);lv_label_set_long_mode(description_label,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_color(description_label,lv_color_hex(skin::muted),0);
    auto *tile=lv_obj_create(card);skin::surface(tile,0x351E27,0);lv_obj_set_pos(tile,20,25);lv_obj_set_size(tile,56,56);lv_obj_set_style_border_width(tile,0,0);lv_obj_remove_flag(tile,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
    auto *icon=lv_label_create(tile);lv_label_set_text(icon,symbol);lv_obj_set_style_text_font(icon,&lv_font_montserrat_32,0);lv_obj_add_flag(icon,LV_OBJ_FLAG_USER_1);lv_obj_center(icon);
    lv_obj_set_style_text_color(icon,lv_color_hex(0xF06A79),0);
    auto *arrow=lv_label_create(card);lv_label_set_text(arrow,LV_SYMBOL_RIGHT);lv_obj_set_style_text_font(arrow,&lv_font_montserrat_20,0);lv_obj_add_flag(arrow,LV_OBJ_FLAG_USER_1);lv_obj_set_pos(arrow,width-38,108);
    return card;
  };
  auto *ssh_card=feature_card("Terminal SSH","Abra uma conexao segura\ncom senha ou chave.",LV_SYMBOL_KEYBOARD,176,175,500,[](lv_event_t*){remote_ui_open();});
  home_default=ssh_card;
  feature_card("Desktop RDP","Acesse seu Windows\ncom teclado e touchpad.",LV_SYMBOL_IMAGE,710,175,510,[](lv_event_t*){rdp_ui_open();});
  feature_card("Cadastros e cofre","Organize hosts, identidades\ne credenciais.",LV_SYMBOL_EYE_CLOSE,176,340,500,[](lv_event_t*){security_ui_open();});
  feature_card("Rede e VPN","Redes Wi-Fi conhecidas\ne conexoes WireGuard.",LV_SYMBOL_WIFI,710,340,510,[](lv_event_t*){remote_ui_open(true);});
  favorites_area = lv_obj_create(home);
  skin::surface(favorites_area, skin::bg, 0);
  lv_obj_set_style_border_width(favorites_area, 0, 0);
  lv_obj_remove_flag(favorites_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(favorites_area, 176, 515);
  lv_obj_set_size(favorites_area, 1045, 94);
  selectors_start();
  security_ui_start(root, focus, reset_input);
  remote_ui_start(root, focus, reset_input);
  rdp_ui_start(root, focus, reset_input);
  lv_group_focus_obj(ssh_card);
  refresh_storage();
  lv_timer_create(
      [](lv_timer_t *) {
        TickMeasure measure;
        uint8_t raw;
        auto now = lv_tick_get();
        auto dispatch = [&](uint8_t raw) {
          if (rdp_ui_focused()) {
            uint8_t key = raw & 127;
            uint16_t scan = key == 0x40   ? 0x1d
                            : key == 0x41 ? 0x38
                            : key == 0x31 ? 0x2a
                                          : 0;
            if (scan)
              rdp_key(scan, raw & 128);
            if (raw == 255) {
              rdp_key(0x1d, false);
              rdp_key(0x38, false);
              rdp_key(0x2a, false);
            }
          }
          apply(input.event(raw, now));
        };
        for (unsigned i = 0; i < 64 && keyboard_event(raw); i++)
          dispatch(raw);
#if CONFIG_TABREMOTO_DIAGNOSTICS
        if (auto *keys = injected_keys.exchange(nullptr)) {
          for (auto key : *keys)
            dispatch(key);
          delete keys;
        }
        if (auto *label = injected_click.exchange(nullptr)) {
          auto *target = find_button(lv_screen_active(), *label);
          delete label;
          if (target) {
            lv_obj_scroll_to_view(target, LV_ANIM_OFF);
            lv_obj_update_layout(lv_screen_active());
            lv_group_focus_obj(target);
            lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
            action_result = 2;
          } else
            action_result = 3;
        }
#endif
#if CONFIG_TABREMOTO_DIAGNOSTICS
        if(!pointer_playing){pointer_playing=pointer_requested.exchange(nullptr);if(pointer_playing){pointer_index=0;pointer_due=now+(*pointer_playing)[0].delay_ms;}}
        if(pointer_playing && int32_t(now-pointer_due)>=0){
          auto sample=(*pointer_playing)[pointer_index];tabremoto_ui_pointer_sample(sample.x,sample.y,sample.pressed);
          if(++pointer_index==pointer_playing->size()){delete pointer_playing;pointer_playing=nullptr;pointer_inflight=false;}
          else pointer_due=now+(*pointer_playing)[pointer_index].delay_ms;
        }
#endif
        apply(input.tick(now));
        static uint32_t ui_due = 0;
        if (int32_t(now - ui_due) < 0)
          return;
        ui_due = now + 50;
        selectors_tick();
        refresh_storage();
        security_ui_tick();
        remote_ui_tick();
        rdp_ui_tick();
        refresh_favorites();
#if CONFIG_TABREMOTO_DIAGNOSTICS
        if (terminal_probe.exchange(false) && remote_ui_terminal_focused()) {
          for (char c : std::string("printf TABREMOTO_UI_%s OK\r"))
            apply(c == '\r' ? 10 : c);
        }
#endif
        static uint32_t refreshed = 0;
        if (now - refreshed >= 1000) {
          refreshed = now;
          auto n = network_status();
          auto v = security_status();
          lv_label_set_text_fmt(
              home_status, "Wi-Fi: %s  |  SD: %s  |  Cofre: %s",
              n.wifi ? n.wifi_ip.c_str() : "desconectado",
              storage_snapshot().mounted ? "pronto" : "ausente",
              v.unlocked ? "aberto" : "bloqueado");
        }
#if CONFIG_TABREMOTO_DIAGNOSTICS
        if (test_requested.exchange(false))
          selftest();
#endif
      },
      10, nullptr);
  lv_refr_now(lv_display_get_default());
  lvgl_port_unlock();
  gpio_hold_dis(BSP_LCD_BACKLIGHT);
  bsp_display_brightness_set(70);
  return true;
}
void ui_terminal_probe() { terminal_probe = true; }

unsigned ui_clicks() { return clicks.load(); }
unsigned ui_flushes() { return flushes.load(); }
bool ui_snapshot() {
  if (!storage_snapshot().mounted || !storage_capture_begin())
    return false;
  struct Unlock {
    ~Unlock() { storage_capture_end(); }
  } unlock;
  if (!lvgl_port_lock(2000))
    return false;
  auto *image = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  lvgl_port_unlock();
  if (!image)
    return false;
  // Keep the row off the console stack: encrypted diagnostic requests retain
  // their parsing buffers while LVGL recursively renders the screen.
  std::unique_ptr<uint8_t[]> rgb_row(new(std::nothrow) uint8_t[1280 * 3]);
  if(!rgb_row){lv_draw_buf_destroy(image);return false;}
  // PPM capture is a rendering diagnostic; it does not prove LCD light output.
  FILE *f = fopen("/sdcard/TabRemoto/cache/screen.ppm", "wb");
  bool ok = f != nullptr;
  if (f) {
    fprintf(f, "P6\n%lu %lu\n255\n", (unsigned long)image->header.w,
            (unsigned long)image->header.h);
    if (image->header.w > 1280)
      ok = false;
    for (unsigned y = 0; ok && y < image->header.h; y++) {
      auto *row = (const uint16_t *)(image->data + y * image->header.stride);
      for (unsigned x = 0; x < image->header.w; x++) {
        uint16_t p = row[x];
        rgb_row[x * 3] = ((p >> 11) & 31) * 255 / 31;
        rgb_row[x * 3 + 1] = ((p >> 5) & 63) * 255 / 63;
        rgb_row[x * 3 + 2] = (p & 31) * 255 / 31;
      }
      if (fwrite(rgb_row.get(), 3, image->header.w, f) != image->header.w)
        ok = false;
    }
    if (fclose(f) != 0)
      ok = false;
  }
  lv_draw_buf_destroy(image);
  return ok;
}
} // namespace tabremoto

bool tabremoto_ui_pointer_sample(int x,int y,bool pressed){
#if CONFIG_TABREMOTO_DIAGNOSTICS
 if(x<0||y<0||x>=1280||y>=720||!lvgl_port_lock(2000))return false;
 struct Sample{int x,y;bool pressed;};static Sample sample{};static lv_indev_t *pointer=nullptr;
 if(!pointer){pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(pointer,lv_display_get_default());lv_indev_set_mode(pointer,LV_INDEV_MODE_EVENT);
  lv_indev_set_user_data(pointer,&sample);lv_indev_set_read_cb(pointer,[](lv_indev_t *device,lv_indev_data_t *data){
   auto *s=static_cast<Sample*>(lv_indev_get_user_data(device));auto *display=lv_indev_get_display(device);
   int rx=s->x,ry=s->y;auto rotation=lv_display_get_rotation(display);
   int width=lv_display_get_original_horizontal_resolution(display),height=lv_display_get_original_vertical_resolution(display);
   if(rotation==LV_DISPLAY_ROTATION_90){rx=s->y;ry=height-s->x-1;}
   else if(rotation==LV_DISPLAY_ROTATION_180){rx=width-s->x-1;ry=height-s->y-1;}
   else if(rotation==LV_DISPLAY_ROTATION_270){rx=width-s->y-1;ry=s->x;}
   data->point={rx,ry};data->state=s->pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
  });
 }
 sample={x,y,pressed};lv_indev_read(pointer);lvgl_port_unlock();return true;
#else
 (void)x;(void)y;(void)pressed;return false;
#endif
}

bool tabremoto_ui_pointer_busy(){
#if CONFIG_TABREMOTO_DIAGNOSTICS
 return tabremoto::pointer_inflight.load();
#else
 return false;
#endif
}
bool tabremoto_ui_pointer_gesture(const std::vector<TabremotoPointerSample> &samples){
#if CONFIG_TABREMOTO_DIAGNOSTICS
 if(samples.empty()||samples.size()>64||!samples.front().pressed||samples.back().pressed)return false;
 unsigned duration=0;for(auto s:samples){if(s.x<0||s.x>=1280||s.y<0||s.y>=720||s.delay_ms>2000)return false;duration+=s.delay_ms;}
 if(duration>5000)return false;
 bool expected=false;if(!tabremoto::pointer_inflight.compare_exchange_strong(expected,true))return false;
 tabremoto::pointer_requested=new std::vector<TabremotoPointerSample>(samples);return true;
#else
 (void)samples;return false;
#endif
}
