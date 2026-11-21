#include "security_ui.hpp"
#include "cJSON.h"
#include "rdp_ui.hpp"
#include "remote_ui.hpp"
#include "security.hpp"
#include "selectors.hpp"
#include "skin.hpp"
#include "vault.hpp"
#include "sdkconfig.h"
#include "esp_lvgl_port.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>
namespace tabremoto {
namespace {
lv_obj_t *panel, *content, *status_label, *inventory, *page_heading, *page_hint, *state_badge;
lv_obj_t *navigation[6]{};
bool layout_preview=false;
std::vector<std::pair<lv_obj_t*,lv_obj_t*>> field_labels;
std::vector<std::pair<std::string,lv_obj_t*>> actions;
std::vector<lv_obj_t*> inventory_rows;
lv_group_t *group;
void (*reset_focus_callback)() = nullptr;
unsigned revision = 0;
std::atomic<bool> open_requested{false};
int page = 0, selected = -1;
std::string selected_id, expected_pin;
lv_obj_t *return_focus = nullptr;
struct Choice {
  lv_obj_t *button = nullptr;
  std::vector<std::string> labels, values;
  unsigned index = 0;
  std::string caption="Selecionar opcao";
  std::string value() const { return values.empty() ? "" : values[index]; }
};
std::vector<Choice *> choices;
void open_choice(Choice *);
void close_choice_modal();
void refine_layout();
lv_obj_t *label_field = nullptr, *host_field = nullptr, *port_field = nullptr,
         *user_field = nullptr, *domain_field = nullptr, *group_field = nullptr,
         *value_field = nullptr, *master_field = nullptr,
         *reason_field = nullptr;
Choice *host_choice = nullptr;
Choice *protocol = nullptr, *reference = nullptr, *auth = nullptr,
       *favorite = nullptr, *kind = nullptr, *transport_choice = nullptr,
       *vpn_choice = nullptr, *vpn_ref_choice = nullptr;
std::string get(cJSON *j, const char *k) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(v) ? v->valuestring : "";
}
void focusable(lv_obj_t *o) {
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *) {
        if (reset_focus_callback)
          reset_focus_callback();
      },
      LV_EVENT_FOCUSED, nullptr);
  lv_group_add_obj(group, o);
  lv_obj_add_event_cb(
      o,
      [](lv_event_t *e) {
        security_activity();
        lv_group_focus_obj(lv_event_get_target_obj(e));
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_set_style_outline_width(o, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(o, lv_color_hex(skin::accent),
                                 LV_STATE_FOCUSED);
}
void clear_secret(lv_obj_t *field) {
  if (!field)
    return;
  auto *p = lv_textarea_get_text(field);
  secure_clear((void *)p, strlen(p));
  lv_textarea_set_text(field, "");
}
void message(const char *text) { lv_label_set_text(status_label, text); }
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                 std::function<void()> fn) {
  auto *b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, width, 45);
  skin::control(b);
  focusable(b);
  skin::button_content(b, text);
  auto *f = new std::function<void()>(std::move(fn));
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        auto action =
            *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
        action();
      },
      LV_EVENT_CLICKED, f);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, f);
  if(parent==content)actions.emplace_back(text,b);
  return b;
}
lv_obj_t *field(const char *caption, int x, int y, int width, int limit,
                bool password = false, int height = 48) {
  auto *l = lv_label_create(content);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, caption);
  auto *f = lv_textarea_create(content);
  lv_obj_set_pos(f, x, y + 32);
  lv_obj_set_size(f, width, height);
  skin::control(f);
  lv_textarea_set_max_length(f, limit);
  lv_textarea_set_one_line(f, height == 48);
  lv_textarea_set_password_mode(f, password);
  focusable(f);
  field_labels.emplace_back(f,l);
  return f;
}
Choice *choice(const std::vector<std::string> &labels,
               const std::vector<std::string> &values, int x, int y,
               int width) {
  auto *c = new Choice;
  c->labels = labels;
  c->values = values;
  c->button = lv_button_create(content);
  lv_obj_set_pos(c->button, x, y);
  lv_obj_set_size(c->button, width, 45);
  skin::control(c->button);
  focusable(c->button);
  skin::button_content(c->button, labels.front().c_str());
  lv_obj_add_event_cb(
      c->button,
      [](lv_event_t *e) {
        auto *c = static_cast<Choice *>(lv_event_get_user_data(e));
        open_choice(c);
      },
      LV_EVENT_CLICKED, c);
  choices.push_back(c);
  return c;
}
void choose(Choice *c, const std::string &value) {
  if (!c)
    return;
  for (unsigned i = 0; i < c->values.size(); i++)
    if (c->values[i] == value) {
      c->index = i;
      lv_label_set_text(lv_obj_get_child(c->button, 0), c->labels[i].c_str());
      return;
    }
}
cJSON *metadata() {
  auto s = security_status();
  return s.metadata ? cJSON_Parse(s.metadata->c_str()) : nullptr;
}
Choice *refs(const char *list, int x, int y, const char *filter = "") {
  std::vector<std::string> labels = {"Sem vinculo"}, values = {""};
  auto *j = metadata();
  auto *a = cJSON_GetObjectItemCaseSensitive(j, list);
  for (auto *r = a ? a->child : nullptr; r; r = r->next) {
    if (*filter && get(r, "kind") != filter)
      continue;
    labels.push_back(get(r, "label"));
    values.push_back(get(r, "id"));
  }
  cJSON_Delete(j);
  return choice(labels, values, x, y, 560);
}
void load_next();
void refresh_inventory() {
  if (!inventory || page < 1 || page > 3)
    return;
  auto *j = metadata();
  const char *collection = page == 1   ? "connections"
                           : page == 2 ? "identities"
                                       : "secrets";
  auto *a = cJSON_GetObjectItemCaseSensitive(j, collection);
  lv_obj_clean(inventory);inventory_rows.clear();
  auto *heading=lv_label_create(inventory);lv_obj_set_pos(heading,12,12);lv_obj_set_width(heading,270);
  lv_label_set_text_fmt(heading,"%s (%d)",page==1?"Conexoes":page==2?"Identidades":"Credenciais",cJSON_GetArraySize(a));
  unsigned index = 0;
  for (auto *r = a ? a->child : nullptr; r; r = r->next, ++index) {
    std::string caption = get(r, "label") + "  |  " +
                          (page == 1   ? get(r, "host")
                           : page == 2 ? get(r, "user")
                                       : get(r, "kind"));
    auto *row =
        button(inventory, caption.c_str(), 12, 54 + index * 80, 270, [index]() {
          selected = int(index) - 1;
          load_next();
        });
    lv_obj_set_size(row,270,70);lv_obj_add_flag(row,LV_OBJ_FLAG_USER_2);
    lv_obj_set_style_bg_color(row,lv_color_hex(0x43232C),LV_STATE_CHECKED);
    lv_obj_set_style_border_color(row,lv_color_hex(skin::accent),LV_STATE_CHECKED);
    if(int(index)==selected)lv_obj_add_state(row,LV_STATE_CHECKED);
    auto *label=lv_obj_get_child(row,0);lv_label_set_text(label,get(r,"label").c_str());
    lv_obj_set_size(label,212,24);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);lv_obj_align(label,LV_ALIGN_TOP_LEFT,32,0);
    auto *detail=lv_label_create(row);lv_label_set_text(detail,(page==1?get(r,"host"):page==2?get(r,"user"):get(r,"kind")).c_str());
    lv_obj_set_size(detail,212,20);lv_label_set_long_mode(detail,LV_LABEL_LONG_DOT);lv_obj_set_pos(detail,32,28);
    lv_obj_set_style_text_font(detail,&lv_font_montserrat_14,0);lv_obj_add_flag(detail,LV_OBJ_FLAG_USER_1);lv_obj_set_style_text_color(detail,lv_color_hex(skin::muted),0);
    inventory_rows.push_back(row);
  }
  if (!index) {
    auto *empty = lv_label_create(inventory);
    lv_obj_set_pos(empty,14,64);lv_obj_set_width(empty,260);
    lv_label_set_text(empty, "Nenhum registro salvo.\nPreencha o formulario e toque em Salvar.");
  }
  cJSON_Delete(j);
}
bool submit(SecurityRequest *r) {
  if(layout_preview){delete r;message("Previa visual: nenhuma gravacao realizada");return false;}
  if (!security_submit(r)) {
    delete r;
    message("Operacao em andamento; tente novamente");
    return false;
  }
  message("Processando... campos sensiveis limpos");
  return true;
}
const char *txt(lv_obj_t *f) { return f ? lv_textarea_get_text(f) : ""; }

std::vector<lv_obj_t *> background_hidden;
std::vector<lv_obj_t *> hide_siblings(lv_obj_t *parent, lv_obj_t *except) {
  std::vector<lv_obj_t *> hidden;
  for (unsigned i = 0; i < lv_obj_get_child_count(parent); i++) {
    auto *o = lv_obj_get_child(parent, i);
    if (o != except && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
      hidden.push_back(o);
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
  }
  return hidden;
}
void restore(const std::vector<lv_obj_t *> &hidden) {
  for (auto *o : hidden)
    if (lv_obj_is_valid(o))
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
void open_overlay() {
  if (!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return;
  return_focus = lv_group_get_focused(group);
  background_hidden = hide_siblings(lv_obj_get_parent(panel), panel);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(panel);
}
void close_overlay() {
  close_choice_modal();
  layout_preview=false;
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  restore(background_hidden);
  background_hidden.clear();
  if (return_focus && lv_obj_is_valid(return_focus))
    lv_group_focus_obj(return_focus);
}
lv_obj_t *confirm(const std::string &text, std::function<void()> action) {
  auto *prior = lv_group_get_focused(group);
  auto *box = lv_obj_create(panel);
  auto hidden = hide_siblings(panel, box);
  lv_obj_set_pos(box, 120, 100);
  lv_obj_set_size(box, 1000, 480);
  lv_obj_set_style_bg_color(box, lv_color_hex(skin::panel), 0);
  lv_obj_set_style_text_color(box, lv_color_hex(skin::text), 0);
  auto *label = lv_label_create(box);
  lv_obj_set_width(label, 940);
  lv_label_set_text(label, text.c_str());
  auto cleanup = [box, hidden, prior]() {
    restore(hidden);
    lv_obj_delete(box);
    if (prior && lv_obj_is_valid(prior))
      lv_group_focus_obj(prior);
  };
  button(box, "Confirmar", 30, 370, 250, [action, cleanup]() {
    action();
    cleanup();
  });
  button(box, "Cancelar", 350, 370, 250, cleanup);
  lv_group_focus_obj(lv_obj_get_child(box, 1));
  return box;
}

void build(int target);
std::function<void()> choice_close;
void close_choice_modal(){auto close=std::move(choice_close);choice_close={};if(close)close();}
void open_choice(Choice *c){
  close_choice_modal();auto *prior=lv_group_get_focused(group);
  auto *box=lv_obj_create(panel);skin::surface(box,skin::panel,0);lv_obj_set_pos(box,250,65);lv_obj_set_size(box,780,590);lv_obj_remove_flag(box,LV_OBJ_FLAG_SCROLLABLE);
  auto hidden=hide_siblings(panel,box);
  auto *heading=lv_label_create(box);lv_label_set_text(heading,c->caption.c_str());lv_obj_set_pos(heading,24,20);lv_obj_set_width(heading,720);
  lv_obj_set_style_text_font(heading,&lv_font_montserrat_24,0);lv_obj_add_flag(heading,LV_OBJ_FLAG_USER_1);
  auto *list=lv_obj_create(box);skin::surface(list,skin::bg,6);lv_obj_set_pos(list,18,68);lv_obj_set_size(list,744,438);lv_obj_set_scroll_dir(list,LV_DIR_VER);
  choice_close=[box,hidden,prior]{restore(hidden);lv_obj_delete(box);if(prior&&lv_obj_is_valid(prior))lv_group_focus_obj(prior);};
  for(unsigned i=0;i<c->labels.size();++i){auto *option=button(list,c->labels[i].c_str(),4,i*56,710,[c,i]{
    c->index=i;lv_label_set_text(lv_obj_get_child(c->button,0),c->labels[i].c_str());close_choice_modal();lv_group_focus_obj(c->button);lv_obj_send_event(c->button,LV_EVENT_VALUE_CHANGED,nullptr);
  });if(i==c->index){lv_obj_set_style_bg_color(option,lv_color_hex(0x43232C),0);lv_obj_set_style_border_color(option,lv_color_hex(skin::accent),0);}}
  auto *cancel=button(box,"Cancelar",24,524,732,close_choice_modal);lv_group_focus_obj(cancel);
}
lv_obj_t *section(lv_obj_t *parent,int x,int y,int width,int height){
 auto *o=lv_obj_create(parent);skin::surface(o,0x20242B,0);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
void note(lv_obj_t *parent,const char *text,int x,int y,int width,bool heading=false){
 auto *label=lv_label_create(parent);lv_label_set_text(label,text);lv_obj_set_pos(label,x,y);lv_obj_set_width(label,width);
 if(heading){lv_obj_set_style_text_font(label,&lv_font_montserrat_24,0);lv_obj_add_flag(label,LV_OBJ_FLAG_USER_1);}else lv_obj_set_style_text_color(label,lv_color_hex(skin::muted),0);
}
void place_field(lv_obj_t *field,lv_obj_t *parent,int x,int y,int width,int height=52){
 if(!field)return;
 lv_obj_set_parent(field,parent);lv_obj_set_pos(field,x,y+30);lv_obj_set_size(field,width,height);
 for(auto entry:field_labels)if(entry.first==field){lv_obj_set_parent(entry.second,parent);lv_obj_set_pos(entry.second,x,y);lv_obj_set_size(entry.second,width,24);lv_label_set_long_mode(entry.second,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(entry.second,lv_color_hex(skin::muted),0);}
}
void place_choice(Choice *c,lv_obj_t *parent,const char *caption,int x,int y,int width){
 if(!c)return;
 c->caption=caption;lv_obj_set_parent(c->button,parent);lv_obj_set_pos(c->button,x,y+30);lv_obj_set_size(c->button,width,48);
 auto *label=lv_obj_get_child(c->button,0);lv_obj_set_width(label,width-68);lv_obj_align(label,LV_ALIGN_CENTER,12,0);
 lv_label_set_text(lv_obj_get_child(c->button,1),LV_SYMBOL_DOWN);note(parent,caption,x,y,width);
}
void place_action(const char *name,lv_obj_t *parent,int x,int y,int width){
 for(auto entry:actions)if(entry.first==name){auto *b=entry.second;
  if(x<0){lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);lv_group_remove_obj(b);return;}
  lv_obj_set_parent(b,parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,48);
  auto *label=lv_obj_get_child(b,0);lv_obj_set_width(label,width-68);lv_obj_align(label,LV_ALIGN_CENTER,12,0);
  if(!strcmp(name,"Salvar")||!strcmp(name,"Desbloquear"))skin::control(b,true);
  if(!strcmp(name,"Excluir...")){lv_obj_set_style_text_color(b,lv_color_hex(0xFF9BA6),0);lv_obj_set_style_border_color(b,lv_color_hex(0x73303A),0);}
  return;
 }
}
void refine_layout(){
 const char *titles[]={"Cadastros e cofre","Conexoes","Identidades","Credenciais","Confianca","Importar dados"};
 const char *hints[]={"Suas credenciais, organizadas e protegidas.","Escolha um host salvo ou crie uma nova conexao.","Usuarios e credenciais para seus servidores.","Senhas, chaves privadas e perfis de rede.","Confira e registre a identidade dos servidores.","Traga seus cadastros de uma exportacao publica."};
 lv_label_set_text(page_heading,titles[page]);lv_label_set_text(page_hint,hints[page]);
 for(int i=0;i<6;++i){if(i==page)lv_obj_add_state(navigation[i],LV_STATE_CHECKED);else lv_obj_remove_state(navigation[i],LV_STATE_CHECKED);}
 // Existing callbacks and fields are retained; only their presentation changes.
 std::vector<lv_obj_t*> children;for(unsigned i=0;i<lv_obj_get_child_count(content);++i)children.push_back(lv_obj_get_child(content,i));
 for(auto *o:children)if(lv_obj_check_type(o,&lv_label_class)){
  bool caption=false;for(auto pair:field_labels)caption=caption||pair.second==o;if(!caption)lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
 }
 if(page==0){
  auto *access=section(content,0,0,500,514),*backup=section(content,520,0,552,514);
  note(access,"Seu cofre",20,20,452,true);note(access,"Senha simples. Credenciais protegidas.",20,60,452);
  place_field(master_field,access,20,110,456);
  place_action("Criar cofre",access,20,214,214);place_action("Desbloquear",access,252,214,224);
  place_action("Bloquear agora",access,20,282,456);place_action("Trocar senha",access,20,350,456);
  note(access,"Use 8 letras ou numeros.\nO cofre bloqueia apos 5 minutos sem uso.",20,424,452);
  note(backup,"Backup e recuperacao",20,20,508,true);note(backup,"Copias dos seus dados, sempre sob seu controle.",20,60,508);
  place_action("Backup criptografado",backup,20,116,508);place_action("Exportar sem segredos",backup,20,180,508);
  place_action("Restaurar backup .trv...",backup,20,244,508);place_action("Recuperar geracao anterior",backup,20,308,508);place_action("Importar cadastros",backup,20,372,508);
  note(backup,"Para restaurar, informe a senha do backup\nno campo ao lado antes de escolher o arquivo.",20,442,508);
 }else{
  auto *editor=section(content,page<=3?316:0,0,page<=3?756:1072,514);
  lv_obj_add_flag(editor,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(editor,LV_DIR_VER);
  for(auto *o:children)if(o!=inventory)lv_obj_set_parent(o,editor);
  if(inventory){lv_obj_set_pos(inventory,0,0);lv_obj_set_size(inventory,296,514);lv_obj_set_style_pad_all(inventory,0,0);lv_obj_set_style_bg_color(inventory,lv_color_hex(0x20242B),0);lv_obj_set_scroll_dir(inventory,LV_DIR_VER);}
  if(page<=3){
   place_field(label_field,editor,16,18,352);
   if(page==1){
    place_field(host_field,editor,388,18,352);place_field(port_field,editor,16,116,164);place_choice(protocol,editor,"Protocolo",196,116,172);place_field(group_field,editor,388,116,352);
    place_choice(reference,editor,"Identidade",16,220,352);place_choice(favorite,editor,"Favorito",388,220,164);place_choice(vpn_ref_choice,editor,"Perfil VPN",576,220,164);
    place_choice(transport_choice,editor,"Rede",16,322,352);place_choice(vpn_choice,editor,"Protecao VPN",388,322,352);
    place_action("Abrir conexao selecionada",editor,16,462,724);
   }else if(page==2){
    place_field(user_field,editor,388,18,352);place_field(domain_field,editor,16,116,352);place_choice(auth,editor,"Autenticacao",388,116,352);
    place_choice(reference,editor,"Credencial salva",16,220,352);place_choice(host_choice,editor,"Vincular a uma conexao",388,220,352);
   }else{
    place_choice(kind,editor,"Tipo de credencial",388,18,352);place_field(value_field,editor,16,116,724,136);
    place_action("Importar .pem / .ppk / .conf",editor,16,372,724);place_action("Ativar VPN selecionada",editor,16,436,352);place_action("Conectar Wi-Fi selecionado",editor,388,436,352);
   }
   int row=page==1?402:page==2?332:306;
   place_action("Salvar",editor,16,row,228);place_action("Novo",editor,260,row,228);place_action("Excluir...",editor,504,row,228);place_action("Carregar proximo",editor,-1,0,0);
  }else if(page==4){
   place_choice(protocol,editor,"Tipo de servidor",16,18,352);place_field(host_field,editor,388,18,652);
   place_field(port_field,editor,16,118,164);place_field(value_field,editor,200,118,840);place_field(reason_field,editor,16,218,1024);
   place_action("Carregar proximo",editor,16,330,320);place_action("Confiar explicitamente...",editor,360,330,680);
   note(editor,"Confira o fingerprint por uma fonte conhecida antes de confiar.\nUma chave alterada exige nova verificacao e o motivo da decisao.",16,414,1024);
  }else{place_field(value_field,editor,16,18,1024,260);place_action("Importar sem sobrescrever IDs",editor,16,346,1024);note(editor,"Use uma exportacao publica, sem senhas nem chaves privadas.",16,430,1024);}
 }
 if(!layout_preview && page!=0 && !security_status().unlocked){
  for(unsigned i=0;i<lv_obj_get_child_count(content);++i)lv_obj_add_flag(lv_obj_get_child(content,i),LV_OBJ_FLAG_HIDDEN);
  auto *locked=section(content,0,0,1072,514);note(locked,"Desbloqueie seu cofre",32,46,980,true);note(locked,"Seus cadastros ficam protegidos enquanto o cofre esta bloqueado.",32,108,980);
  auto *unlock=button(locked,"Desbloquear cofre",32,188,420,[]{build(0);});lv_group_focus_obj(unlock);
 }
}

void build(int target);
void load_next() {
  const char *list = page == 1   ? "connections"
                     : page == 2 ? "identities"
                     : page == 3 ? "secrets"
                                 : "trust";
  auto *j = metadata();
  auto *a = cJSON_GetObjectItemCaseSensitive(j, list);
  int n = cJSON_GetArraySize(a);
  if (!n) {
    cJSON_Delete(j);
    message("Nenhum registro");
    return;
  }
  selected = (selected + 1) % n;
  auto *r = cJSON_GetArrayItem(a, selected);
  selected_id = get(r, "id");
  if (label_field)
    lv_textarea_set_text(label_field, get(r, "label").c_str());
  if (page == 1) {
    lv_textarea_set_text(host_field, get(r, "host").c_str());
    char p[8];
    snprintf(p, sizeof(p), "%d",
             cJSON_GetObjectItemCaseSensitive(r, "port")->valueint);
    lv_textarea_set_text(port_field, p);
    lv_textarea_set_text(group_field, get(r, "group").c_str());
    choose(protocol, get(r, "protocol"));
    if (page == 1) {
      choose(transport_choice,
             get(r, "transport").empty() ? "auto" : get(r, "transport"));
      choose(vpn_ref_choice, get(r, "vpn_ref"));
      choose(vpn_choice,
             cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "vpn_required"))
                 ? "true"
                 : "false");
    }
    choose(reference, get(r, "identity_id"));
    choose(favorite,
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "favorite"))
               ? "true"
               : "false");
  }
  if (page == 2) {
    lv_textarea_set_text(user_field, get(r, "user").c_str());
    lv_textarea_set_text(domain_field, get(r, "domain").c_str());
    choose(auth, get(r, "auth"));
    choose(reference, get(r, "secret_id"));
    auto *hosts = cJSON_GetObjectItemCaseSensitive(j, "connections");
    for (auto *host = hosts ? hosts->child : nullptr; host; host = host->next)
      if (get(host, "identity_id") == selected_id) {
        choose(host_choice, get(host, "id"));
        break;
      }
  }
  if (page == 3) {
    choose(kind, get(r, "kind"));
    clear_secret(value_field);
    message("Segredo existente: conteudo oculto. Preencha para substituir.");
  }
  if (page == 4) {
    choose(protocol, get(r, "protocol"));
    if (page == 1) {
      choose(transport_choice,
             get(r, "transport").empty() ? "auto" : get(r, "transport"));
      choose(vpn_ref_choice, get(r, "vpn_ref"));
      choose(vpn_choice,
             cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "vpn_required"))
                 ? "true"
                 : "false");
    }
    lv_textarea_set_text(host_field, get(r, "host").c_str());
    char p[8];
    snprintf(p, sizeof(p), "%d",
             cJSON_GetObjectItemCaseSensitive(r, "port")->valueint);
    lv_textarea_set_text(port_field, p);
    lv_textarea_set_text(value_field, get(r, "fingerprint").c_str());
    expected_pin = get(r, "fingerprint");
    lv_textarea_set_text(reason_field, "");
  }
  cJSON_Delete(j);
  for(unsigned i=0;i<inventory_rows.size();++i){if(int(i)==selected)lv_obj_add_state(inventory_rows[i],LV_STATE_CHECKED);else lv_obj_remove_state(inventory_rows[i],LV_STATE_CHECKED);}
}
void save_form() {
  auto *r = new SecurityRequest;
  r->id = selected_id;
  r->label = txt(label_field);
  if (page == 1) {
    r->action = SecurityAction::Connection;
    r->host = txt(host_field);
    r->port = atoi(txt(port_field));
    r->protocol = protocol->value();
    r->ref = reference->value();
    r->favorite = favorite->value() == "true";
    r->group = txt(group_field);
    r->transport = transport_choice->value();
    r->vpn_ref = vpn_ref_choice->value();
    r->vpn_required = vpn_choice->value() == "true";
  }
  if (page == 2) {
    r->action = SecurityAction::Identity;
    r->user = txt(user_field);
    r->domain = txt(domain_field);
    r->auth = auth->value();
    r->host_ref = host_choice->value();
    r->ref = r->auth == "ask" ? "" : reference->value();
  }
  if (page == 3) {
    r->action = SecurityAction::Secret;
    r->kind = kind->value();
    if (strlen(txt(value_field)) >= r->value.size()) {
      delete r;
      message("Conteudo excede 8192 bytes");
      return;
    }
    snprintf(r->value.data(), r->value.size(), "%s", txt(value_field));
    clear_secret(value_field);
  }
  submit(r);
}
void build(int target) {
  close_choice_modal();
  if (value_field && page == 3)
    clear_secret(value_field);
  if (master_field)
    clear_secret(master_field);
  // Delete objects first; LVGL removes them from the focus group.
  field_labels.clear();actions.clear();inventory_rows.clear();
  lv_obj_clean(content);
  inventory = nullptr;
  for (auto *c : choices)
    delete c;
  choices.clear();
  page = target;
  selected = -1;
  selected_id.clear();
  expected_pin.clear();
  label_field = host_field = port_field = user_field = domain_field =
      group_field = value_field = master_field = reason_field = nullptr;
  host_choice = nullptr;
  protocol = reference = auth = favorite = kind = transport_choice =
      vpn_choice = vpn_ref_choice = nullptr;
  if (page == 0) {
    master_field =
        field("Senha mestra: 8 letras ou numeros", 20, 5, 560, 8, true);
    lv_textarea_set_accepted_chars(
        master_field,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    auto request = [](SecurityAction action) {
      auto *r = new SecurityRequest;
      r->action = action;
      snprintf(r->password.data(), r->password.size(), "%s", txt(master_field));
      clear_secret(master_field);
      submit(r);
    };
    button(content, "Criar cofre", 20, 100, 260,
           [request]() { request(SecurityAction::Create); });
    button(content, "Desbloquear", 320, 100, 260,
           [request]() { request(SecurityAction::Unlock); });
    button(content, "Bloquear agora", 20, 165, 260, []() {
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Lock;
      submit(r);
    });
    button(content, "Trocar senha", 320, 165, 260,
           [request]() { request(SecurityAction::Password); });
    button(content, "Recuperar geracao anterior", 20, 230, 560, [request]() {
      confirm("Recuperar pode retornar cadastros e confianca anteriores.\nUse "
              "a senha da geracao de backup. Nenhum arquivo sera apagado.",
              [request]() { request(SecurityAction::Recover); });
    });
    button(content, "Exportar sem segredos", 620, 100, 540, []() {
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Export;
      submit(r);
    });
    button(content, "Backup criptografado", 620, 165, 540, []() {
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Backup;
      submit(r);
    });
    button(content, "Restaurar backup .trv...", 620, 230, 540, [] {
      if (!security_status().unlocked) {
        lv_label_set_text(status_label,
                          "Desbloqueie o cofre antes de restaurar um backup");
        return;
      }
      if (!*txt(master_field)) {
        message("Digite a senha do backup no campo acima antes de selecionar o "
                "arquivo");
        lv_group_focus_obj(master_field);
        return;
      }
      select_import_file(panel, group, {".trv"}, [](const std::string &file) {
        confirm(
            "Restaurar o arquivo " + file.substr(file.rfind('/') + 1) +
                "?\nOs cadastros atuais serao substituidos somente apos "
                "autenticar o backup.\nA senha digitada acima sera usada para "
                "autenticar o backup. A senha atual do cofre sera mantida.",
            [file] {
              auto *r = new SecurityRequest;
              r->action = SecurityAction::Restore;
              r->file = file;
              snprintf(r->password.data(), r->password.size(), "%s",
                       txt(master_field));
              clear_secret(master_field);
              submit(r);
            });
      });
    });

    button(content, "Importar cadastros", 620, 295, 540, []() { build(5); });
    auto *l = lv_label_create(content);
    lv_obj_set_pos(l, 20, 365);
    lv_obj_set_width(l, 1130);
    lv_label_set_text(
        l, "Bloqueio automatico: 5 minutos sem uso ou SD ausente.\nSegredos "
           "ficam criptografados no SD. Nenhuma senha e enviada pela "
           "USB em texto puro.\nCadastros SSH abrem o terminal; RDP nativo "
           "esta em validacao.\nAa + aspas, "
           "soltar Aa, depois vogal: trema (ü).\nSenha e chave de servidor sao "
           "separadas da senha mestra.");
  } else if (page <= 3) {
    label_field = field("Nome", 20, 5, 560, 80);
    if (page == 1) {
      host_field = field("Hostname / IP", 620, 5, 540, 253);
      port_field = field("Porta", 20, 95, 260, 5);
      lv_textarea_set_accepted_chars(port_field, "0123456789");
      lv_textarea_set_text(port_field, "22");
      protocol = choice({"SSH", "RDP"}, {"ssh", "rdp"}, 320, 120, 260);
      lv_obj_add_event_cb(
          protocol->button,
          [](lv_event_t *) {
            auto value = std::string(txt(port_field));
            if (protocol->value() == "rdp" && value == "22")
              lv_textarea_set_text(port_field, "3389");
            else if (protocol->value() == "ssh" && value == "3389")
              lv_textarea_set_text(port_field, "22");
          },
          LV_EVENT_VALUE_CHANGED, nullptr);
      group_field = field("Grupo", 620, 95, 540, 80);
      reference = refs("identities", 20, 210);
      favorite = choice({"Nao", "Sim"}, {"false", "true"},
                        620, 210, 250);
      vpn_ref_choice = refs("secrets", 900, 210, "vpn");
      lv_obj_set_width(vpn_ref_choice->button, 260);
    }
    if (page == 2) {
      user_field = field("Usuario", 620, 5, 540, 128);
      domain_field = field("Dominio (opcional)", 20, 95, 560, 128);
      auth = choice({"Pedir credencial", "Senha salva", "Chave salva"},
                    {"ask", "password", "key"}, 620, 120, 540);
      reference = refs("secrets", 20, 210);
      host_choice = refs("connections", 620, 210);
      auto *caption = lv_label_create(content);
      lv_obj_set_pos(caption, 620, 185);
      lv_label_set_text(caption, "Vincular tambem ao host (opcional)");
    }
    if (page == 3) {
      kind = choice({"Senha", "Chave privada (conteudo)", "Passphrase", "VPN",
                     "CA", "Wi-Fi (JSON)"},
                    {"password", "key", "passphrase", "vpn", "ca", "wifi"}, 620,
                    30, 540);
      value_field = field("Conteudo secreto (nao exibido depois de salvar)", 20,
                          95, 1140, 8192, true, 130);
    }
    button(content, "Salvar", 20, 290, 260, save_form);
    button(content, "Novo", 320, 290, 260, []() { build(page); });
    button(content, "Carregar proximo", 620, 290, 260, load_next);
    button(content, "Excluir...", 920, 290, 240, []() {
      if (selected_id.empty()) {
        message("Carregue um registro antes");
        return;
      }
      auto id = selected_id;
      auto list = page == 1   ? "connections"
                  : page == 2 ? "identities"
                              : "secrets";
      confirm("Excluir o registro selecionado?\nRegistros vinculados serao "
              "preservados; remova os vinculos primeiro.",
              [id, list]() {
                auto *r = new SecurityRequest;
                r->action = SecurityAction::Remove;
                r->id = id;
                r->collection = list;
                submit(r);
              });
    });
  } else if (page == 4) {
    protocol = choice({"Chave SSH", "Pin de certificado RDP"}, {"ssh", "rdp"},
                      20, 20, 560);
    host_field = field("Hostname / IP exato", 620, 0, 540, 253);
    port_field = field("Porta", 20, 80, 260, 5);
    lv_textarea_set_accepted_chars(port_field, "0123456789");
    lv_textarea_set_text(port_field, "22");
    value_field = field("Fingerprint SHA-256: 64 caracteres hexadecimais", 320,
                        80, 840, 64);
    lv_textarea_set_accepted_chars(value_field, "0123456789abcdef");
    reason_field = field("Motivo da decisao", 20, 170, 1140, 160);
    button(content, "Carregar proximo", 20, 280, 300, load_next);
    button(content, "Confiar explicitamente...", 360, 280, 420, []() {
      auto protocol_value = protocol->value(),
           host = normalize_endpoint(txt(host_field)),
           fingerprint = std::string(txt(value_field)),
           reason = std::string(txt(reason_field));
      int port = atoi(txt(port_field));
      // Obtain the previous pin from the current immutable snapshot, never from
      // a user editable field. Core verifies it again on commit.
      auto *j = metadata();
      auto *a = cJSON_GetObjectItemCaseSensitive(j, "trust");
      std::string previous;
      for (auto *t = a ? a->child : nullptr; t; t = t->next)
        if (get(t, "protocol") == protocol_value && get(t, "host") == host &&
            cJSON_GetObjectItemCaseSensitive(t, "port")->valueint == port)
          previous = get(t, "fingerprint");
      cJSON_Delete(j);
      confirm(
          "Decisao manual para " + host + ":" + std::to_string(port) +
              "\nAnterior: " + (previous.empty() ? "desconhecido" : previous) +
              "\nNovo: " + fingerprint + "\nMotivo: " + reason +
              "\nNenhuma verificacao por CA ou conexao de rede foi realizada.",
          [=]() {
            auto *r = new SecurityRequest;
            r->action = SecurityAction::Trust;
            r->protocol = protocol_value;
            r->host = host;
            r->port = port;
            r->fingerprint = fingerprint;
            r->previous = previous;
            r->reason = reason;
            submit(r);
          });
    });
    auto *l = lv_label_create(content);
    lv_obj_set_pos(l, 20, 360);
    lv_obj_set_width(l, 1140);
    lv_label_set_text(
        l, "Chave desconhecida exige decisao. Chave alterada bloqueia novas "
           "autenticacoes.\nPins SSH e RDP sao separados por endpoint e porta. "
           "Aliases nao herdam confianca.");
  } else {
    value_field = field("Importar JSON publico (schema 1, sem segredos)", 20, 5,
                        1140, 8192, false, 250);
    button(content, "Importar sem sobrescrever IDs", 20, 310, 560, []() {
      auto *r = new SecurityRequest;
      r->action = SecurityAction::Import;
      if (strlen(txt(value_field)) >= r->value.size()) {
        delete r;
        message("Conteudo excede 8192 bytes");
        return;
      }
      snprintf(r->value.data(), r->value.size(), "%s", txt(value_field));
      submit(r);
    });
  }
  if (page >= 1 && page <= 3) {
    inventory = lv_obj_create(content);
    skin::surface(inventory, skin::bg, 4);
    lv_obj_set_pos(inventory, 20, 410);
    lv_obj_set_size(inventory, 1130, 125);
    refresh_inventory();
    if (page == 1) {
      button(content, "Abrir conexao selecionada", 20, 350, 350, []() {
        if (selected_id.empty()) {
          message("Carregue um cadastro antes");
          return;
        }
        if (protocol->value() == "rdp")
          rdp_ui_profile(selected_id);
        else
          remote_ui_profile(selected_id);
      });
      transport_choice =
          choice({"Rede automatica", "Somente Wi-Fi", "Somente Ethernet"},
                 {"auto", "wifi", "ethernet"}, 400, 350, 350);
      vpn_choice = choice({"VPN opcional", "VPN OBRIGATORIA"},
                          {"false", "true"}, 790, 350, 370);
    }
    if (page == 3) {
      button(content, "Importar .pem / .ppk / .conf", 20, 350, 350, [] {
        if (!security_status().unlocked) {
          message("Desbloqueie o cofre antes de importar");
          return;
        }
        select_import_file(
            panel, group, {".pem", ".ppk", ".key", ".conf"},
            [](const std::string &path) {
              auto *r = new SecurityRequest;
              r->action = SecurityAction::SecretFile;
              r->file = path;
              r->label = path.substr(path.rfind('/') + 1);
              auto lower = path;
              std::transform(lower.begin(), lower.end(), lower.begin(),
                             [](unsigned char c) { return std::tolower(c); });
              r->kind = lower.size() >= 5 &&
                                lower.compare(lower.size() - 5, 5, ".conf") == 0
                            ? "vpn"
                            : "key";
              submit(r);
            });
      });
      button(content, "Ativar VPN selecionada", 390, 350, 350, [] {
        auto *r = new SecurityRequest;
        r->action = SecurityAction::Vpn;
        r->id = selected_id;
        r->protocol = "wifi";
        submit(r);
      });
      button(content, "Conectar Wi-Fi selecionado", 770, 350, 390, [] {
        auto *r = new SecurityRequest;
        r->action = SecurityAction::Wifi;
        r->id = selected_id;
        submit(r);
      });
    }
  }
  refine_layout();
  auto *first=master_field?master_field:label_field?label_field:host_field?host_field:value_field;
  if(first && !lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && (layout_preview||page==0||security_status().unlocked))lv_group_focus_obj(first);
}

} // namespace
void security_ui_start(lv_obj_t *root, lv_group_t *g, void (*reset_focus)()) {
  reset_focus_callback = reset_focus;
  group = g;
  panel = lv_obj_create(root);
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_set_size(panel, 1280, 720);
  skin::surface(panel, skin::bg, 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_text_color(panel, lv_color_hex(skin::text), 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  auto *rail=lv_obj_create(panel);skin::surface(rail,0x191C21,8);lv_obj_set_size(rail,148,720);lv_obj_set_style_radius(rail,0,0);lv_obj_set_style_border_width(rail,0,0);lv_obj_remove_flag(rail,LV_OBJ_FLAG_SCROLLABLE);
  auto *brand=lv_label_create(rail);lv_label_set_text(brand,"TR");lv_obj_set_pos(brand,35,22);lv_obj_set_style_text_font(brand,&lv_font_montserrat_32,0);lv_obj_add_flag(brand,LV_OBJ_FLAG_USER_1);lv_obj_set_style_text_color(brand,lv_color_hex(skin::accent),0);
  const char *names[]={"Cofre","Conexoes","Identidades","Segredos","Confianca","Dados"};
  const char *symbols[]={LV_SYMBOL_EYE_CLOSE,LV_SYMBOL_LIST,LV_SYMBOL_KEYBOARD,LV_SYMBOL_DIRECTORY,LV_SYMBOL_OK,LV_SYMBOL_DOWNLOAD};
  for(unsigned i=0;i<6;++i){navigation[i]=button(rail,names[i],4,96+i*80,124,[i](){build(i);});lv_obj_set_height(navigation[i],70);lv_obj_add_flag(navigation[i],LV_OBJ_FLAG_USER_2);
   auto *label=lv_obj_get_child(navigation[i],0);lv_obj_set_width(label,104);lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);lv_obj_add_flag(label,LV_OBJ_FLAG_USER_1);lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,0);
   lv_label_set_text(lv_obj_get_child(navigation[i],1),symbols[i]);lv_obj_align(lv_obj_get_child(navigation[i],1),LV_ALIGN_TOP_MID,0,0);lv_obj_set_style_text_font(lv_obj_get_child(navigation[i],1),&lv_font_montserrat_20,0);
   lv_obj_set_style_bg_color(navigation[i],lv_color_hex(0x611620),LV_STATE_CHECKED);lv_obj_set_style_border_color(navigation[i],lv_color_hex(skin::accent),LV_STATE_CHECKED);
  }
  button(rail,"Voltar",4,636,124,[](){if(master_field)clear_secret(master_field);if(page==3)clear_secret(value_field);close_overlay();});
  page_heading=lv_label_create(panel);lv_obj_set_pos(page_heading,176,24);lv_obj_set_width(page_heading,780);lv_obj_set_style_text_font(page_heading,&lv_font_montserrat_32,0);lv_obj_add_flag(page_heading,LV_OBJ_FLAG_USER_1);
  page_hint=lv_label_create(panel);lv_obj_set_pos(page_hint,178,70);lv_obj_set_width(page_hint,1050);lv_obj_set_style_text_color(page_hint,lv_color_hex(skin::muted),0);
  state_badge=lv_label_create(panel);lv_obj_set_pos(state_badge,996,34);lv_obj_set_width(state_badge,250);lv_obj_set_style_text_align(state_badge,LV_TEXT_ALIGN_RIGHT,0);
  status_label=lv_label_create(panel);lv_obj_set_pos(status_label,178,112);lv_obj_set_size(status_label,1068,46);lv_label_set_long_mode(status_label,LV_LABEL_LONG_DOT);
  content=lv_obj_create(panel);lv_obj_set_pos(content,176,174);lv_obj_set_size(content,1072,522);skin::surface(content,skin::bg,0);lv_obj_set_style_border_width(content,0,0);lv_obj_remove_flag(content,LV_OBJ_FLAG_SCROLLABLE);
  build(0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
void security_ui_open() { open_requested = true; }
bool security_ui_layout_preview(unsigned target){
#if CONFIG_TABREMOTO_DIAGNOSTICS
 if(target>5||!lvgl_port_lock(2000))return false;
 layout_preview=true;open_overlay();build(target);lv_label_set_text(status_label,"Previa visual: cofre pessoal permanece bloqueado.");lvgl_port_unlock();return true;
#else
 (void)target;return false;
#endif
}
bool security_ui_selftest() {
  if (!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return false;
  auto *original = lv_group_get_focused(group);
  bool ok = true;
  layout_preview=true;
  for (int i = 0; i < 6; i++) {
    build(i);
    ok = ok && lv_obj_get_child_count(content) > 0;
    if (i == 0) {
      lv_textarea_set_text(master_field, "Ab123456");
      ok = ok && lv_textarea_get_password_mode(master_field) &&
           strlen(txt(master_field)) == 8;
      clear_secret(master_field);
    }
    if (i == 3) {
      lv_textarea_set_text(value_field, "public-test-secret");
      ok = ok && lv_textarea_get_password_mode(value_field);
      clear_secret(value_field);
    }
  }
  open_overlay();
  build(0);
  auto inside = [](lv_obj_t *o, lv_obj_t *parent) {
    while (o && o != parent)
      o = lv_obj_get_parent(o);
    return o == parent;
  };
  for (unsigned i = 0; i < 40; i++) {
    lv_group_focus_next(group);
    ok = ok && inside(lv_group_get_focused(group), panel);
    lv_group_focus_prev(group);
    ok = ok && inside(lv_group_get_focused(group), panel);
    lv_group_focus_next(group);
  }
  auto *modal = confirm("Teste de foco; nenhuma gravacao", []() {});
  for (unsigned i = 0; i < 8; i++) {
    lv_group_focus_next(group);
    ok = ok && inside(lv_group_get_focused(group), modal);
  }
  lv_obj_send_event(lv_obj_get_child(modal, 2), LV_EVENT_CLICKED, nullptr);
  close_overlay();
  lv_group_focus_obj(original);
  return ok;
}
void security_ui_tick() {
  if (open_requested.exchange(false)) {
    revision = ~0u;
    layout_preview=false;
    open_overlay();
    build(0);
  }
  if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
    return;
  auto s = security_status();
  if (s.revision == revision)
    return;
  revision = s.revision;
  lv_label_set_text(state_badge,s.busy?"Processando...":s.unlocked?"Cofre aberto":"Cofre bloqueado");
  lv_obj_set_style_text_color(state_badge,lv_color_hex(s.unlocked?0x8DD6AA:skin::muted),0);
  lv_label_set_text(status_label,layout_preview?"Previa visual: cofre pessoal permanece bloqueado.":s.message.c_str());
  if (s.unlocked && !s.busy)
    refresh_inventory();
  if (!layout_preview && !s.unlocked && page != 0 && !s.busy) {
    build(0);
  }
}
} // namespace tabremoto
