#include "remote_ui.hpp"
#include "cJSON.h"
#include "network.hpp"
#include "profiles.hpp"
#include "sdkconfig.h"
#include "security.hpp"
#include "selectors.hpp"
#include "skin.hpp"
#include "ssh.hpp"
#include "ui.hpp"
#include "vault.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
namespace tabremoto {
namespace {
static bool wifi_results_pending=false;
lv_obj_t *panel, *body, *status, *terminal_obj, *terminal_label, *host, *user,
    *password, *port_field, *ssid, *wifi_pass, *path, *local, *vpn_conf,
    *file_list, *local_files, *ethernet_address, *ethernet_mask,
    *ethernet_gateway, *ethernet_dns;
lv_group_t *group;
void (*reset)();
std::vector<lv_obj_t *> hidden;
lv_obj_t *previous;
std::atomic<int> requested{-1};
int page = 0;
std::string route_transport="auto",route_profile;
std::string manual_key, profile, rendered_local_listing,
    rendered_remote_listing;
lv_font_t mono;
lv_obj_t *session_disconnect, *session_switch, *connect_button, *vpn_list;
std::string rendered_vpns;
bool session_fullscreen = false, file_session_pending = false;
std::vector<lv_obj_t *> session_hidden;
std::shared_ptr<const TerminalFrame> terminal_frame;
constexpr int cell_width = 12, line_height = 22;
const lv_font_t *terminal_font;
bool fixed_glyph(const lv_font_t *, lv_font_glyph_dsc_t *d, uint32_t ch,
                 uint32_t next) {
  bool found = terminal_font->get_glyph_dsc(terminal_font, d, ch, next);
  d->adv_w = cell_width;
  return found;
}
void append_utf8(std::string &text, uint32_t ch) {
  if (ch < 0x80)
    text += char(ch);
  else if (ch < 0x800) {
    text += char(0xc0 | (ch >> 6));
    text += char(0x80 | (ch & 63));
  } else if (ch < 0x10000) {
    text += char(0xe0 | (ch >> 12));
    text += char(0x80 | ((ch >> 6) & 63));
    text += char(0x80 | (ch & 63));
  } else {
    text += char(0xf0 | (ch >> 18));
    text += char(0x80 | ((ch >> 12) & 63));
    text += char(0x80 | ((ch >> 6) & 63));
    text += char(0x80 | (ch & 63));
  }
}
void draw_terminal(lv_event_t *event) {
  if (!terminal_frame)
    return;
  auto *layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(terminal_label, &area);
  auto &f = *terminal_frame;
  auto style = [&](int r, int c) {
    const auto &cell = f.cells[r * f.columns + c];
    bool cursor = f.cursor_visible && r == f.history + f.cursor_row &&
                  c == f.cursor_column;
    return std::make_pair(cursor ? cell.background : cell.foreground,
                          cursor ? cell.foreground : cell.background);
  };
  for (int r = 0; r < f.rows + f.history; ++r) {
    int y = area.y1 + r * line_height;
    if (y + mono.line_height < layer->_clip_area.y1 || y > layer->_clip_area.y2)
      continue;
    for (int c = 0; c < f.columns;) {
      const auto colors = style(r, c);
      const auto &first = f.cells[r * f.columns + c];
      int end = c + 1;
      // One label per style run rather than thousands of labels per frame.
      if (first.width == 1)
        while (end < f.columns && f.cells[r * f.columns + end].width == 1 &&
               style(r, end) == colors &&
               f.cells[r * f.columns + end].underline == first.underline)
          ++end;
      else
        end = std::min(f.columns, c + std::max(1, int(first.width)));
      lv_area_t rect = {area.x1 + c * cell_width, y,
                        area.x1 + end * cell_width - 1, y + line_height - 1};
      if (colors.second != 0x0c0f14) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(colors.second);
        d.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &d, &rect);
      }
      std::string text;
      if (first.width == 1) {
        for (int i = c; i < end; ++i) {
          uint32_t ch = f.cells[r * f.columns + i].character;
          append_utf8(text, ch < 32 || ch == 0xffffffffu ? 32 : ch);
        }
      } else if (first.character > 32 && first.character != 0xffffffffu)
        append_utf8(text, first.character);
      while (!text.empty() && text.back() == ' ')
        text.pop_back();
      if (!text.empty()) {
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &mono;
        d.color = lv_color_hex(colors.first);
        d.text = text.c_str();
        d.text_local = 1;
        d.flag = LV_TEXT_FLAG_EXPAND;
        auto glyph_area = rect;
        glyph_area.y2 = y + mono.line_height - 1;
        lv_draw_label(layer, &d, &glyph_area);
      }
      if (first.underline) {
        auto line = rect;
        line.y1 = line.y2 - 1;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(colors.first);
        lv_draw_rect(layer, &d, &line);
      }
      c = end;
    }
  }
}
lv_obj_t *button(lv_obj_t *, const char *, int, int, int,
                 std::function<void()>);
void session_layout(bool active) {
  if (!terminal_obj || active == session_fullscreen)
    return;
  session_fullscreen = active;
  if (active) {
    for (auto *parent : {panel, body})
      for (unsigned i = 0; i < lv_obj_get_child_count(parent); i++) {
        auto *o = lv_obj_get_child(parent, i);
        if (o != body && o != terminal_obj &&
            !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
          session_hidden.push_back(o);
          lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
      }
    lv_obj_set_pos(body, 0, 0);
    lv_obj_set_size(body, 1280, 720);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(terminal_obj, 16, 54);
    lv_obj_set_size(terminal_obj, 1248, 656);
    lv_obj_set_style_pad_all(terminal_obj, 8, 0);
    lv_obj_set_style_outline_width(terminal_obj, 0, LV_STATE_FOCUSED);
    session_disconnect =
        button(panel, "Desconectar", 1040, 4, 220, [] { ssh_disconnect(); });
    session_switch =
        button(panel, "Sessoes", 12, 4, 240, [] { ui_sessions_open(); });
    lv_group_focus_obj(terminal_obj);
  } else {
    if (session_switch) {
      lv_obj_delete(session_switch);
      session_switch = nullptr;
    }
    if (session_disconnect) {
      lv_obj_delete(session_disconnect);
      session_disconnect = nullptr;
    }
    for (auto *o : session_hidden) {
      if (lv_obj_is_valid(o))
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    session_hidden.clear();
    lv_obj_set_pos(body, 20, 130);
    lv_obj_set_size(body, 1240, 560);
    skin::surface(body);
    lv_obj_set_pos(terminal_obj, 10, 160);
    lv_obj_set_size(terminal_obj, 1150, 330);
    lv_group_focus_obj(connect_button);
  }
}
std::string remote_test_detail;
unsigned ssh_revision = 0, net_revision = 0;
lv_obj_t *verify_button, *trust_modal, *trust_confirm;
std::string modal_fingerprint, prompted_fingerprint, notice;
unsigned submitted_revision = 0;
bool profile_pending = false;
SshStatus::Phase last_phase = SshStatus::Idle;
std::atomic<SshRequest *> ui_request{nullptr};
std::atomic<bool> ui_probe{false}, ui_accept{false};

void focus(lv_obj_t *o) {
  lv_group_add_obj(group, o);
  lv_obj_set_style_outline_width(o, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(o, lv_color_hex(skin::accent),
                                 LV_STATE_FOCUSED);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *) {
        if (reset)
          reset();
      },
      LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *e) {
        security_activity();
        lv_group_focus_obj(lv_event_get_target_obj(e));
      },
      LV_EVENT_CLICKED, nullptr);
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                 std::function<void()> action) {
  auto *b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, width, 46);
  skin::control(b);
  skin::button_content(b, text);
  focus(b);
  auto *fn = new std::function<void()>(action);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        auto call =
            *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
        call();
      },
      LV_EVENT_CLICKED, fn);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, fn);
  return b;
}
lv_obj_t *field(const char *label, int x, int y, int width, int n,
                bool secret = false, int h = 44) {
  auto *l = lv_label_create(body);
  lv_label_set_text(l, label);
  lv_obj_set_pos(l, x, y);
  auto *o = lv_textarea_create(body);
  lv_obj_set_pos(o, x, y + 32);
  lv_obj_set_size(o, width, h);
  skin::control(o);
  lv_textarea_set_one_line(o, h == 44);
  lv_textarea_set_max_length(o, n);
  lv_textarea_set_password_mode(o, secret);
  if (secret)
    lv_textarea_set_password_show_time(o, 0);
  focus(o);
  return o;
}
const char *text(lv_obj_t *o) { return o ? lv_textarea_get_text(o) : ""; }
void clear(lv_obj_t *o) {
  if (o) {
    const char *t = text(o);
    secure_clear((void *)t, strlen(t));
    lv_textarea_set_text(o, "");
  }
}
void show_message(const char *m) {
  notice = m;
  lv_label_set_text(status, m);
}
void close() {
  clear(password);
  clear(wifi_pass);
  clear(vpn_conf);
  manual_key.clear();
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  for (auto *o : hidden)
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  hidden.clear();
  if (previous && lv_obj_is_valid(previous))
    lv_group_focus_obj(previous);
}
void list() {
  FileRequest r;
  r.remote = text(path);
  if (!ssh_files(r))
    show_message("Conecte SSH antes de listar arquivos");
}
void build(int p) {
  net_revision = ~0u;
  ssh_revision = ~0u;
  session_layout(false);
  terminal_frame.reset();
  clear(password);
  clear(wifi_pass);
  clear(vpn_conf);
  lv_obj_clean(body);
  rendered_local_listing.clear();
  rendered_remote_listing.clear();
  host = user = password = port_field = ssid = wifi_pass = path = local =
      vpn_conf = file_list = local_files = terminal_obj = terminal_label =
          nullptr;
  vpn_list = nullptr;
  rendered_vpns.clear();
  page = p;
  notice.clear();
  if (page == 0) {
    host = field("Servidor", 10, 0, 350, 253);
    port_field = field("Porta", 375, 0, 90, 5);
    lv_textarea_set_text(port_field, "22");
    user = field("Usuario", 480, 0, 270, 128);
    password = field("Senha ou passphrase", 760, 0, 400, 256, true);
    if (!profile.empty()) {
      lv_textarea_set_placeholder_text(host, "Cadastro selecionado no cofre");
      lv_textarea_set_placeholder_text(user, "Identidade do cadastro");
      auto state = security_status();
      auto *j = state.metadata ? cJSON_Parse(state.metadata->c_str()) : nullptr;
      auto string = [](cJSON *o, const char *key) {
        auto *v = cJSON_GetObjectItemCaseSensitive(o, key);
        return cJSON_IsString(v) ? v->valuestring : "";
      };
      auto *connections = cJSON_GetObjectItemCaseSensitive(j, "connections");
      for (auto *c = connections ? connections->child : nullptr; c; c = c->next)
        if (profile == string(c, "id")) {
          lv_textarea_set_text(host, string(c, "host"));
          auto *port = cJSON_GetObjectItemCaseSensitive(c, "port");
          if (cJSON_IsNumber(port))
            lv_textarea_set_text(port_field,
                                 std::to_string(port->valueint).c_str());
          auto identity = std::string(string(c, "identity_id"));
          auto *ids = cJSON_GetObjectItemCaseSensitive(j, "identities");
          for (auto *i = ids ? ids->child : nullptr; i; i = i->next)
            if (identity == string(i, "id"))
              lv_textarea_set_text(user, string(i, "user"));
        }
      cJSON_Delete(j);
      lv_obj_add_state(host, LV_STATE_DISABLED);
      lv_obj_add_state(user, LV_STATE_DISABLED);
      lv_obj_add_state(port_field, LV_STATE_DISABLED);
    }
    button(body, "Hosts salvos", 250, 100, 230, [] {
      select_saved_host(panel, group, "ssh", [](const std::string &id) {
        profile = id;
        manual_key.clear();
        build(0);
      });
    });
    button(body, "Chave .pem / .ppk", 490, 100, 230, [] {
      select_import_file(
          panel, group, {".pem", ".ppk", ".key"}, [](const std::string &file) {
            manual_key = file;
            show_message(
                ("Chave selecionada: " + file.substr(file.rfind('/') + 1) +
                 "; informe a passphrase se houver")
                    .c_str());
          });
    });
    connect_button = button(body, "Conectar SSH", 10, 100, 230, []() {
      notice.clear();
      prompted_fingerprint.clear();
      if (!profile.empty()) {
        if (!security_status().unlocked) {
          show_message("Desbloqueie o cofre para usar este cadastro.");
          return;
        }
        submitted_revision = security_status().revision;
        profile_pending = true;
        auto *r = new SecurityRequest;
        r->action = SecurityAction::Ssh;
        r->id = profile;
        snprintf(r->password.data(), r->password.size(), "%s", text(password));
        clear(password);
        if (!security_submit(r)) {
          delete r;
          show_message("Desbloqueie o cofre ou aguarde");
        }
        return;
      }
      auto *r = new SshRequest;
      r->transport=route_transport;
      r->vpn_profile=route_profile;
      r->require_vpn=!route_profile.empty();
      r->key_file = manual_key;
      snprintf(r->passphrase.data(), r->passphrase.size(), "%s",
               text(password));
      r->host = text(host);
      r->user = text(user);
      r->port = atoi(text(port_field));
      snprintf(r->password.data(), r->password.size(), "%s", text(password));
      clear(password);
      if (!ssh_connect(r)) {
        delete r;
        show_message("Conexao ocupada ou campos invalidos");
      }
    });
    skin::control(connect_button, true);
    verify_button = button(body, "Verificar chave...", 730, 100, 230, []() {
      auto s = ssh_status();
      if (s.phase != SshStatus::Verify) {
        show_message("Nao ha chave pendente");
        return;
      }
      if (trust_modal)
        return;
      auto *modal = lv_obj_create(panel);
      trust_modal = modal;
      modal_fingerprint = s.fingerprint;
      skin::surface(modal);
      lv_obj_set_pos(modal, 40, 100);
      lv_obj_set_size(modal, 1160, 470);
      lv_obj_set_style_bg_color(modal, lv_color_hex(skin::panel), 0);
      lv_obj_set_style_text_color(modal, lv_color_hex(skin::text), 0);
      auto *l = lv_label_create(modal);
      lv_obj_set_width(l, 1100);
      lv_label_set_text(
          l,
          (s.detail + "\nHost: " + s.host + "\nSHA256 (hex): " + s.fingerprint +
           "\nConfirme somente apos verificar a chave por fonte confiavel.")
              .c_str());
      std::vector<lv_obj_t *> hide;
      for (unsigned i = 0; i < lv_obj_get_child_count(panel); i++) {
        auto *o = lv_obj_get_child(panel, i);
        if (o != modal && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
          hide.push_back(o);
          lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
      }
      auto cleanup = [modal, hide]() {
        trust_modal = nullptr;
        trust_confirm = nullptr;
        modal_fingerprint.clear();
        for (auto *o : hide)
          lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete(modal);
      };
      trust_confirm =
          button(modal, "Confirmar chave", 30, 350, 400, [cleanup, s]() {
            ssh_accept(s.fingerprint);
            cleanup();
          });
      button(modal, "Cancelar", 500, 350, 300, [cleanup]() {
        ssh_disconnect();
        cleanup();
      });
      lv_group_focus_obj(lv_obj_get_child(modal, 1));
    });
    button(body, "Desconectar", 970, 100, 190, []() { ssh_disconnect(); });
    terminal_obj = lv_obj_create(body);
    skin::surface(terminal_obj, 0x0C0F14, 12);
    lv_obj_set_pos(terminal_obj, 10, 160);
    lv_obj_set_size(terminal_obj, 1150, 330);
    lv_obj_set_style_bg_color(terminal_obj, lv_color_hex(0x0C0F14), 0);
    lv_obj_set_style_text_color(terminal_obj, lv_color_hex(skin::text), 0);
    focus(terminal_obj);
    lv_obj_set_style_text_font(terminal_obj, &mono, 0);
    terminal_label = lv_obj_create(terminal_obj);
    lv_obj_remove_style_all(terminal_label);
    lv_obj_remove_flag(terminal_label,
                       static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                  LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_height(terminal_label, 29 * line_height);
    lv_obj_add_event_cb(terminal_label, draw_terminal, LV_EVENT_DRAW_MAIN,
                        nullptr);
    lv_obj_set_width(terminal_label, 100 * cell_width);
    lv_obj_set_style_text_font(terminal_label, &mono, 0);

    button(body,("Rota: "+(route_profile.empty()?route_transport:route_profile)).c_str(),780,500,380,[]{select_network_route(panel,group,[](const std::string &transport,const std::string &vpn){route_transport=transport;route_profile=vpn;show_message(("Rota selecionada: "+transport+(vpn.empty()?"":" / "+vpn)).c_str());});});
    auto *hint = lv_label_create(body);
    lv_obj_set_pos(hint, 10, 505);
    lv_label_set_text(hint,
                      "Shift+1..0/-/+: F1..F12 | Ctrl+Tab: sessoes");
  } else if (page == 1) {
    path = field("Diretorio ou arquivo remoto", 10, 0, 740, 512);
    lv_textarea_set_text(path, ".");
    local = field("Nome local: uploads/ ou downloads/", 780, 0, 380, 128);
    button(body, "Listar SFTP", 10, 85, 220, [] { list(); });
    button(body, "Baixar SFTP", 250, 85, 220, [] {
      FileRequest r;
      r.operation = FileRequest::Download;
      r.remote = text(path);
      r.local = text(local);
      show_message(ssh_files(r) ? "Arquivo adicionado a fila"
                                : "Conecte SSH ou aguarde espaco na fila");
    });
    button(body, "Enviar SFTP", 490, 85, 220, [] {
      FileRequest r;
      r.operation = FileRequest::Upload;
      r.remote = text(path);
      r.local = text(local);
      show_message(ssh_files(r) ? "Arquivo adicionado a fila"
                                : "Conecte SSH ou aguarde espaco na fila");
    });
    button(body, "Baixar SCP", 730, 85, 205, [] {
      FileRequest r;
      r.operation = FileRequest::ScpDownload;
      r.remote = text(path);
      r.local = text(local);
      show_message(ssh_files(r) ? "Arquivo adicionado a fila"
                                : "Conecte SSH ou aguarde espaco na fila");
    });
    button(body, "Enviar SCP", 955, 85, 205, [] {
      FileRequest r;
      r.operation = FileRequest::ScpUpload;
      r.remote = text(path);
      r.local = text(local);
      show_message(ssh_files(r) ? "Arquivo adicionado a fila"
                                : "Conecte SSH ou aguarde espaco na fila");
    });
    button(body, "Cancelar fila e desconectar", 10, 160, 430,
           [] { ssh_cancel_files(); });
    button(body, "Conectar", 460, 145, 170, [] {
      file_session_pending = true;
      build(0);
      lv_group_focus_obj(host);
    });
    button(body, "Atualizar arquivos locais", 650, 145, 480,
           [] { ssh_local_list(); });
    file_list = lv_obj_create(body);
    lv_obj_set_pos(file_list, 10, 205);
    lv_obj_set_size(file_list, 555, 300);
    local_files = lv_obj_create(body);
    skin::surface(local_files, skin::bg, 4);
    lv_obj_set_pos(local_files, 590, 205);
    lv_obj_set_size(local_files, 565, 300);
    ssh_local_list();
    lv_obj_set_style_text_color(file_list, lv_color_hex(skin::text), 0);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(skin::bg), 0);
    ssh_revision = 0;
  } else if (page == 2) {
    ssid = field("SSID Wi-Fi", 10, 0, 560, 32);
    wifi_pass = field("Senha Wi-Fi", 600, 0, 560, 63, true);
    button(body, "Conectar e salvar", 10, 95, 350, [] {
      bool ok = network_wifi(text(ssid), text(wifi_pass));
      clear(wifi_pass);
      show_message(ok ? "Conectando; a rede sera salva apos obter IP" : "Credenciais Wi-Fi invalidas");
    });
    button(body, "Iniciar Ethernet Baseus NCM", 400, 95, 440, [] {
      show_message(network_ethernet() ? "USB host NCM iniciado"
                                      : "USB/Ethernet falhou");
    });
    button(body,"Buscar redes",850,95,310,[]{
      if(!network_wifi_scan()){show_message("Aguarde a operacao Wi-Fi atual");return;}
      wifi_results_pending=true;show_message("Buscando redes em segundo plano...");
    });
    button(body,"Escolher rede",850,155,310,[]{
      select_wifi_network(panel,group,false,[](const std::string &name,bool secured){
        lv_textarea_set_text(ssid,name.c_str());clear(wifi_pass);
        for(const auto &known:profiles_wifi())if(known.ssid==name&&known.protected_network&&secured){show_message(network_wifi(name,known.password)?"Conectando a rede conhecida":"Wi-Fi ocupado");return;}
        if(!secured){show_message(network_wifi(name,"")?"Conexao manual a rede aberta":"Wi-Fi ocupado");return;}
        show_message("Informe a senha e toque Conectar e salvar");lv_group_focus_obj(wifi_pass);
      });
    });
    button(body,"Redes salvas",850,215,310,[]{
      select_wifi_network(panel,group,true,[](const std::string &name,bool){
        for(const auto &known:profiles_wifi())if(known.ssid==name){lv_textarea_set_text(ssid,name.c_str());show_message(network_wifi(name,known.password)?"Conectando a rede salva":"Wi-Fi ocupado");break;}
      });
    });
    button(body, "Salvar Wi-Fi no cofre", 10, 155, 400, [] {
      if (!security_status().unlocked) {
        show_message("Desbloqueie o cofre antes de salvar");
        return;
      }
      if (!strlen(text(ssid))) {
        show_message("Informe o SSID");
        return;
      }
      auto *j = cJSON_CreateObject();
      cJSON_AddStringToObject(j, "ssid", text(ssid));
      cJSON_AddStringToObject(j, "password", text(wifi_pass));
      auto *encoded = cJSON_PrintUnformatted(j);
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Secret;
      r->label = text(ssid);
      r->kind = "wifi";
      if (encoded)
        snprintf(r->value.data(), r->value.size(), "%s", encoded);
      auto *pass = cJSON_GetObjectItemCaseSensitive(j, "password");
      if (cJSON_IsString(pass))
        secure_clear(pass->valuestring, strlen(pass->valuestring));
      cJSON_Delete(j);
      bool ok = encoded && security_submit(r);
      if (encoded) {
        secure_clear(encoded, strlen(encoded));
        cJSON_free(encoded);
      }
      if (!ok)
        delete r;
      clear(wifi_pass);
      show_message(ok ? "Salvando; confira o resultado no cofre"
                      : "Cofre ocupado");
    });
    auto *ethernet_title = lv_label_create(body);
    lv_obj_set_pos(ethernet_title, 10, 245);
    lv_label_set_text(ethernet_title, "Endereco Ethernet (Wi-Fi usa DHCP)");
    ethernet_address = field("Ethernet: IP fixo", 10, 300, 220, 15);
    ethernet_mask = field("Mascara", 245, 300, 220, 15);
    ethernet_gateway = field("Gateway", 480, 300, 220, 15);
    ethernet_dns = field("DNS IPv4", 715, 300, 220, 15);
    button(body, "Aplicar IP", 950, 332, 210, [] {
      show_message(
          network_ethernet_ip(false, text(ethernet_address),
                              text(ethernet_mask), text(ethernet_gateway),
                              text(ethernet_dns))
              ? "Configurando IP fixo"
              : "Inicie Ethernet e confira IP, mascara, gateway e DNS");
    });
    button(body, "Usar DHCP Ethernet", 440, 155, 400, [] {
      show_message(network_ethernet_ip(true) ? "Solicitando DHCP"
                                             : "Inicie Ethernet antes");
    });
    auto *l = lv_label_create(body);
    lv_obj_set_pos(l, 10, 395);
    lv_obj_set_width(l, 1140);
    lv_label_set_text(
        l, "Wi-Fi e Ethernet sao transportes fisicos separados.\nNenhuma senha "
           "e salva em NVS compartilhada.\nEthernet Baseus exige USB-A Host, "
           "CDC-NCM "
           "compativel e link/DHCP reais.\nPerda ou troca de interface encerra "
           "a sessao SSH.");
  } else {
    auto *intro = lv_label_create(body);
    lv_obj_set_pos(intro, 10, 0);
    lv_obj_set_width(intro, 1140);
    lv_label_set_text(intro,
                      "WireGuard: ate quatro tuneis simultaneos. Selecione um "
                      "arquivo .conf ou um perfil do cofre.");
    button(body, "Abrir .conf: Wi-Fi", 10, 65, 360, [] {
      select_import_file(panel, group, {".conf"}, [](const std::string &file) {
        show_message(network_vpn_file(file, "wifi")
                         ? "Lendo e conectando perfil em segundo plano"
                         : "Aguarde a operacao VPN atual");
      });
    });
    button(body, "Abrir .conf: Ethernet", 390, 65, 360, [] {
      select_import_file(panel, group, {".conf"}, [](const std::string &file) {
        show_message(network_vpn_file(file, "ethernet")
                         ? "Lendo perfil Ethernet"
                         : "Aguarde a operacao VPN atual");
      });
    });
    button(body, "Importar .conf no cofre", 770, 65, 390, [] {
      if (!security_status().unlocked) {
        show_message("Desbloqueie o cofre para importar");
        return;
      }
      select_import_file(panel, group, {".conf"}, [](const std::string &file) {
        auto *r = new SecurityRequest;
        r->action = SecurityAction::SecretFile;
        r->file = file;
        r->kind = "vpn";
        r->label = file.substr(file.rfind('/') + 1);
        if (!security_submit(r)) {
          delete r;
          show_message("Cofre ocupado");
        } else
          show_message(
              "Importando perfil cifrado; arquivo original permanece no SD");
      });
    });
    vpn_list = lv_obj_create(body);
    skin::surface(vpn_list);
    lv_obj_set_pos(vpn_list, 10, 160);
    lv_obj_set_size(vpn_list, 1155, 325);
    auto *help = lv_label_create(body);
    lv_obj_set_pos(help, 10, 485);
    lv_obj_set_width(help, 1140);
    lv_label_set_text(help, "Um peer IPv4 por arquivo; AllowedIPs sobrepostos "
                            "sao recusados.\nParar mantem bloqueadas as rotas. "
                            "So um perfil pode fornecer DNS global.");
  }
}
void refresh_vpns(const NetworkStatus &network) {
  if (!vpn_list)
    return;
  auto sec = security_status();
  std::string key = sec.unlocked && sec.metadata ? *sec.metadata : "";
  for (const auto &vpn : network.tunnels)
    key += vpn.id + (vpn.up ? "+" : "-") + vpn.ip;
  if (key == rendered_vpns)
    return;
  rendered_vpns = key;
  lv_obj_clean(vpn_list);
  unsigned row = 0;
  auto *metadata = sec.metadata ? cJSON_Parse(sec.metadata->c_str()) : nullptr;
  auto *secrets = cJSON_GetObjectItemCaseSensitive(metadata, "secrets");
  auto value = [](cJSON *item, const char *name) {
    auto *v = cJSON_GetObjectItemCaseSensitive(item, name);
    return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
  };
  for (const auto &vpn : network.tunnels) {
    auto id = vpn.id, label = id;
    for (auto *item = secrets ? secrets->child : nullptr; item;
         item = item->next)
      if (value(item, "id") == id)
        label = value(item, "label");
    auto *text = lv_label_create(vpn_list);
    lv_obj_set_pos(text, 0, row * 76 + 10);
    lv_obj_set_width(text, 580);
    lv_obj_set_height(text,60);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_label_set_text(text,
                      (label + " | " +
                       (vpn.up ? "Conectado" : "Parado / handshake pendente") +
                       "\n" + vpn.ip + " | " + vpn.transport)
                          .c_str());
    button(vpn_list, "Parar", 600, row * 76, 180,
           [id] { network_vpn_stop(false, id); });
    button(vpn_list, "Fechar e liberar rotas", 800, row * 76, 290, [id] {
      confirm_action(
          panel, group,
          "Fechar este perfil e liberar seus CIDRs para a rede fisica?\nAs "
          "rotas deixam de ser protegidas por este tunel.",
          [id] { network_vpn_stop(true, id); });
    });
    ++row;
  }
  for (auto *item = secrets ? secrets->child : nullptr; item;
       item = item->next) {
    if (value(item, "kind") != "vpn")
      continue;
    auto id = value(item, "id");
    bool open = false;
    for (const auto &v : network.tunnels)
      if (v.id == id)
        open = true;
    if (open)
      continue;
    button(vpn_list, ("Abrir: " + value(item, "label")).c_str(), 0, row++ * 76,
           1090, [id] {
             auto *request = new SecurityRequest;
             request->action = SecurityAction::Vpn;
             request->id = id;
             request->protocol = "wifi";
             if (!security_submit(request)) {
               delete request;
               show_message("Cofre bloqueado ou ocupado");
             }
           });
  }
  cJSON_Delete(metadata);
  if (!row) {
    auto *text = lv_label_create(vpn_list);
    lv_obj_set_width(text, 1080);
    lv_label_set_text(
        text, "Nenhum tunel aberto. Importe um .conf de /TabRemoto/imports ou "
              "desbloqueie o cofre para escolher os perfis salvos.");
  }
}
void open(int p) {
  if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
    previous = lv_group_get_focused(group);
    for (unsigned i = 0; i < lv_obj_get_child_count(lv_obj_get_parent(panel));
         i++) {
      auto *o = lv_obj_get_child(lv_obj_get_parent(panel), i);
      if (o != panel && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        hidden.push_back(o);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
    }
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel);
  }
  build(p);
  auto *f = p == 0 && ssh_status().phase == SshStatus::Ready ? terminal_obj
            : p == 0 && !profile.empty()                     ? password
            : host                                           ? host
            : path                                           ? path
            : ssid                                           ? ssid
                                                             : vpn_conf;
  if (f)
    lv_group_focus_obj(f);
}
} // namespace
void remote_ui_start(lv_obj_t *root, lv_group_t *g, void (*reset_input)()) {
  remote_ui_set_font(&lv_font_montserrat_20);
  group = g;
  reset = reset_input;
  panel = lv_obj_create(root);
  lv_obj_set_size(panel, 1280, 720);
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  skin::surface(panel, skin::bg, 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_text_color(panel, lv_color_hex(skin::text), 0);
  const char *labels[] = {"Terminal SSH", "Arquivos", "Rede", "WireGuard"};
  for (int i = 0; i < 4; i++)
    button(panel, labels[i], i * 204+(i?32:0), 0, i?188:220, [i]() { build(i); });
  button(panel, "Sessoes", 848, 0, 188, [] { ui_sessions_open(); });
  button(panel, "Voltar", 1052, 0, 220, [] { close(); });
  status = lv_label_create(panel);
  lv_obj_set_pos(status, 10, 57);
  lv_obj_set_width(status, 1190);
  body = lv_obj_create(panel);
  lv_obj_set_pos(body, 20, 130);
  lv_obj_set_size(body, 1240, 560);
  skin::surface(body);
  lv_obj_set_style_text_color(body, lv_color_hex(skin::text), 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
std::string remote_ui_test_detail() { return remote_test_detail; }
bool remote_ui_selftest() {
  if (!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return false;
  auto *original = lv_group_get_focused(group);
  bool ok = true;
  remote_test_detail.clear();
  for (int p = 0; p < 4; ++p) {
    open(p);
    bool page_ok = lv_obj_get_child_count(body) > 0;
    auto *secret = p == 0   ? password
                   : p == 2 ? wifi_pass
                   : p == 3 ? vpn_conf
                            : nullptr;
    if (secret)
      page_ok = page_ok && lv_textarea_get_password_mode(secret);
    if (p == 2) {
      lv_obj_update_layout(body);
      for (auto *field :
           {ethernet_address, ethernet_mask, ethernet_gateway, ethernet_dns}) {
        lv_area_t field_area;
        lv_obj_get_coords(field, &field_area);
        for (unsigned i = 0; i < lv_obj_get_child_count(body); i++) {
          auto *label = lv_obj_get_child(body, i);
          if (!lv_obj_check_type(label, &lv_label_class))
            continue;
          lv_area_t label_area;
          lv_obj_get_coords(label, &label_area);
          page_ok = page_ok && !(field_area.x1 <= label_area.x2 &&
                                 label_area.x1 <= field_area.x2 &&
                                 field_area.y1 <= label_area.y2 &&
                                 label_area.y1 <= field_area.y2);
        }
      }
    }
    for (unsigned i = 0; i < 40; ++i) {
      lv_group_focus_next(group);
      auto *o = lv_group_get_focused(group);
      while (o && o != panel)
        o = lv_obj_get_parent(o);
      page_ok = page_ok && o == panel;
      lv_group_focus_prev(group);
    }
    remote_test_detail +=
        "pagina" + std::to_string(p) + "=" + std::to_string(page_ok) + ";";
    ok = ok && page_ok;
  }
  close();
  if (original && lv_obj_is_valid(original))
    lv_group_focus_obj(original);
  return ok;
}
bool remote_ui_submit_on_enter() {
  if (page != 0 || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN) || trust_modal)
    return false;
  auto *focused = lv_group_get_focused(group);
  if (focused != host && focused != user && focused != password &&
      focused != port_field)
    return false;
  lv_obj_send_event(connect_button, LV_EVENT_CLICKED, nullptr);
  return true;
}
bool remote_ui_test_connect(SshRequest *r) {
  SshRequest *empty = nullptr;
  return ui_request.compare_exchange_strong(empty, r);
}
bool remote_ui_test_accept(const std::string &pin) {
  auto s = ssh_status();
  if (s.phase != SshStatus::Verify || s.fingerprint != pin)
    return false;
  ui_accept = true;
  return true;
}
bool remote_ui_trust_visible() {
  return trust_modal && !lv_obj_has_flag(trust_modal, LV_OBJ_FLAG_HIDDEN);
}
bool remote_ui_rendered_marker() {
  if (!terminal_label || !terminal_frame)
    return false;
  std::string visible;
  for (const auto &c : terminal_frame->cells)
    visible += c.character < 128 ? char(c.character ? c.character : ' ') : '?';
  return visible.find("TABREMOTO_UI_OK") != std::string::npos;
}
bool remote_ui_session_fullscreen() {
  return session_fullscreen && session_disconnect &&
         lv_obj_get_width(terminal_obj) >= 1200 &&
         lv_obj_get_height(terminal_obj) >= 650;
}
void remote_ui_hide() {
  if (panel && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    close();
}
void remote_ui_files_open() {
  profile.clear();
  requested = 1;
}
void remote_ui_open(bool network) {
  file_session_pending = false;
  profile.clear();
  manual_key.clear();
  requested = network ? 2 : 0;
}
void remote_ui_profile(const std::string &id) {
  profile = id;
  requested = 0;
}
void remote_ui_set_font(const lv_font_t *fallback) {
  terminal_font = fallback;
  mono = *fallback;
  mono.get_glyph_dsc = fixed_glyph;
  if (terminal_obj)
    lv_obj_set_style_text_font(terminal_obj, &mono, 0);
  if (terminal_label)
    lv_obj_set_style_text_font(terminal_label, &mono, 0);
}

bool remote_ui_terminal_focused() {
  return terminal_obj && lv_group_get_focused(group) == terminal_obj &&
         ui_panel_front(panel);
}
void remote_ui_tick() {
  int p = requested.exchange(-1);
  if (p >= 0)
    open(p);
#if CONFIG_TABREMOTO_DIAGNOSTICS
  if (auto *r = ui_request.exchange(nullptr)) {
    std::unique_ptr<SshRequest> owned(r);
    profile.clear();
    manual_key = r->key_file;
    open(0);
    lv_textarea_set_text(host, r->host.c_str());
    lv_textarea_set_text(user, r->user.c_str());
    lv_textarea_set_text(password, r->passphrase[0] ? r->passphrase.data()
                                                    : r->password.data());
    lv_textarea_set_text(port_field, std::to_string(r->port).c_str());
    lv_group_focus_obj(password);
    remote_ui_submit_on_enter();
  }
#endif
  if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return;
  auto net = network_status();
  if(page==2 && wifi_results_pending && !network_wifi_busy()){
    wifi_results_pending=false;
    select_wifi_network(panel,group,false,[](const std::string &name,bool secured){
      lv_textarea_set_text(ssid,name.c_str());clear(wifi_pass);
      for(const auto &known:profiles_wifi())if(known.ssid==name&&known.protected_network&&secured){show_message(network_wifi(name,known.password)?"Conectando a rede conhecida":"Wi-Fi ocupado");return;}
      if(!secured){show_message(network_wifi(name,"")?"Conexao manual a rede aberta":"Wi-Fi ocupado");return;}
      show_message("Informe a senha e toque Conectar e salvar");lv_group_focus_obj(wifi_pass);
    });
  }
  auto ssh = ssh_status();
  if (page == 3)
    refresh_vpns(net);
  if (profile_pending) {
    auto sec = security_status();
    if (sec.revision != submitted_revision && !sec.busy) {
      profile_pending = false;
      if (ssh.phase != SshStatus::Connecting &&
          ssh.phase != SshStatus::Authenticating &&
          ssh.phase != SshStatus::Verify && ssh.phase != SshStatus::Ready)
        show_message(sec.message.c_str());
    }
  }
  if (file_session_pending && ssh.phase == SshStatus::Ready) {
    file_session_pending = false;
    build(1);
  }
  if (page == 0 && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
    if (ssh.phase == SshStatus::Verify && !trust_modal &&
        prompted_fingerprint != ssh.fingerprint) {
      prompted_fingerprint = ssh.fingerprint;
      // Invoke the exact visible verification control, not an implicit trust
      // decision.
      if (verify_button)
        lv_obj_send_event(verify_button, LV_EVENT_CLICKED, nullptr);
    }
    if (ssh.phase == SshStatus::Ready && last_phase != SshStatus::Ready &&
        terminal_obj) {
      notice.clear();
      lv_group_focus_obj(terminal_obj);
    }
  }
  if (page == 0 && terminal_obj && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    session_layout(ssh.phase == SshStatus::Ready);
  if (ssh.phase == SshStatus::Connecting && last_phase != SshStatus::Connecting)
    prompted_fingerprint.clear();
  last_phase = ssh.phase;
#if CONFIG_TABREMOTO_DIAGNOSTICS
  if (ui_accept.exchange(false) && trust_confirm)
    lv_obj_send_event(trust_confirm, LV_EVENT_CLICKED, nullptr);
#endif
  if (net.revision != net_revision || ssh.revision != ssh_revision) {
    char info[256];
    snprintf(info, sizeof(info),
             "Wi-Fi: %s (%d dBm) | Ethernet: %s | VPN: %s | %s\n%s",
             net.wifi_ip.c_str(), net.rssi, net.ethernet_ip.c_str(),
             net.vpn         ? "ativo"
             : net.vpn_armed ? "bloqueado"
                             : "desligado",
             ssh.detail.c_str(),
             page >= 2 ? net.detail.c_str() : ssh.key_type.c_str());
    if (page == 3) {
      std::string age =
          net.handshake_age_ms == 0xffffffffu
              ? "pendente"
              : std::to_string(net.handshake_age_ms / 1000) + " s";
      snprintf(info, sizeof(info),
               "VPN: %s | Handshake: %s | UDP Rx: %llu B | Tx: %llu B\n%s",
               net.vpn         ? "ativo"
               : net.vpn_armed ? "rotas bloqueadas"
                               : "desligado",
               age.c_str(), (unsigned long long)net.rx,
               (unsigned long long)net.tx, net.vpn_detail.c_str());
    }
    if (!notice.empty())
      snprintf(info, sizeof(info), "%s", notice.c_str());
    if (page == 1 && ssh.transfer)
      snprintf(info, sizeof(info), "Transferindo: %llu / %llu bytes | Fila: %u",
               (unsigned long long)ssh.transfer_bytes,
               (unsigned long long)ssh.transfer_total, ssh.queued);
    lv_label_set_text(status, info);
    net_revision = net.revision;
    if (terminal_label && ssh.screen && terminal_frame != ssh.screen) {
      bool follow = lv_obj_get_scroll_bottom(terminal_obj) <= line_height;
      terminal_frame = ssh.screen;
      lv_obj_set_height(terminal_label,
                        (terminal_frame->history + terminal_frame->rows) *
                            line_height);
      lv_obj_invalidate(terminal_label);
      if (follow)
        lv_obj_scroll_to_y(terminal_obj, LV_COORD_MAX, LV_ANIM_OFF);
    }
    if (local_files && ssh.local_listing != rendered_local_listing) {
      rendered_local_listing = ssh.local_listing;
      lv_obj_clean(local_files);
      auto *array = cJSON_Parse(ssh.local_listing.c_str());
      for (int i = 0; i < std::min(128, cJSON_GetArraySize(array)); i++) {
        auto *item = cJSON_GetArrayItem(array, i);
        auto *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(name))
          continue;
        std::string value = name->valuestring;
        button(local_files, value.c_str(), 0, i * 48, 500,
               [value] { lv_textarea_set_text(local, value.c_str()); });
      }
      cJSON_Delete(array);
    }
    if (file_list && ssh.listing != rendered_remote_listing) {
      rendered_remote_listing = ssh.listing;
      lv_obj_clean(file_list);
      auto *a = cJSON_Parse(ssh.listing.c_str());
      int n = std::min(cJSON_GetArraySize(a), 64);
      for (int i = 0; i < n; i++) {
        auto *item = cJSON_GetArrayItem(a, i);
        auto *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(name))
          continue;
        std::string value = name->valuestring;
        bool dir =
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "directory"));
        button(file_list, ((dir ? "[DIR] " : "") + value).c_str(), 0, i * 48,
               500, [value, dir]() {
                 std::string next = text(path);
                 if (!next.empty() && next.back() != '/')
                   next += '/';
                 next += value;
                 lv_textarea_set_text(path, next.c_str());
                 if (dir)
                   list();
                 else
                   lv_textarea_set_text(local, value.c_str());
               });
      }
      cJSON_Delete(a);
    }
    ssh_revision = ssh.revision;
  }
}
} // namespace tabremoto
