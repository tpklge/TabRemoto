#include "selectors.hpp"
#include "network.hpp"
#include "profiles.hpp"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "imports.hpp"
#include "rdp.hpp"
#include "security.hpp"
#include "security_ui.hpp"
#include "skin.hpp"
#include "ssh.hpp"
#include "storage.hpp"
#include "ui.hpp"
#include <atomic>
#include <cstdio>
#include <memory>
namespace tabremoto {
namespace {
lv_obj_t *modal = nullptr, *list = nullptr, *caption = nullptr,
         *prior = nullptr;
lv_group_t *focus;
std::vector<lv_obj_t *> hidden;
std::function<void(const std::string &)> chosen;
std::string folder;
std::vector<std::string> filters;
struct Job {
  unsigned generation;
  std::string path;
  std::vector<std::string> extensions;
};
struct Result {
  unsigned generation;
  std::string error;
  std::vector<ImportEntry> entries;
};
QueueHandle_t jobs;
SemaphoreHandle_t lock;
std::unique_ptr<Result> result;
unsigned generation = 0;
void worker(void *) {
  for (;;) {
    Job *raw = nullptr;
    if (xQueueReceive(jobs, &raw, portMAX_DELAY) != pdTRUE)
      continue;
    std::unique_ptr<Job> job(raw);
    auto next = std::make_unique<Result>();
    next->generation = job->generation;
    bool lease = storage_capture_begin();
    if (lease) {
      import_list(job->path, job->extensions, next->entries, next->error);
      storage_capture_end();
    } else
      next->error = "SD ocupado ou ausente";
    xSemaphoreTake(lock, portMAX_DELAY);
    result = std::move(next);
    xSemaphoreGive(lock);
  }
}
void close() {
  ++generation;
  if (modal)
    lv_obj_delete(modal);
  modal = list = caption = nullptr;
  for (auto *o : hidden)
    if (lv_obj_is_valid(o))
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  hidden.clear();
  if (prior && lv_obj_is_valid(prior))
    lv_group_focus_obj(prior);
  chosen = {};
}
lv_obj_t *button(lv_obj_t *parent, const std::string &text, int x, int y,
                 int width, std::function<void()> fn) {
  auto *b = lv_button_create(parent);
  skin::control(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, width, 46);
  lv_group_add_obj(focus, b);
  skin::button_content(b, text.c_str());
  auto *action = new std::function<void()>(std::move(fn));
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        security_activity();
        lv_group_focus_obj(lv_event_get_target_obj(e));
        auto action =
            *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
        action();
      },
      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, action);
  return b;
}
void open(lv_obj_t *parent, lv_group_t *group, const std::string &title,
          std::function<void(const std::string &)> fn) {
  if (modal)
    close();
  focus = group;
  prior = lv_group_get_focused(group);
  auto *root = lv_obj_get_screen(parent);
  modal = lv_obj_create(root);
  skin::surface(modal, skin::bg, 12);
  lv_obj_set_pos(modal, 28, 24);
  lv_obj_set_size(modal, 1224, 672);
  for (unsigned i = 0; i < lv_obj_get_child_count(root); i++) {
    auto *o = lv_obj_get_child(root, i);
    if (o != modal && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
      hidden.push_back(o);
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
  }
  chosen = std::move(fn);
  caption = lv_label_create(modal);
  lv_obj_set_pos(caption, 10, 5);
  lv_obj_set_width(caption, 930);
  lv_label_set_text(caption, title.c_str());
  auto *cancel_button = button(modal, "Cancelar", 970, 0, 200, close);
  lv_group_focus_obj(cancel_button);
  list = lv_obj_create(modal);
  skin::surface(list);
  lv_obj_set_pos(list, 8, 125);
  lv_obj_set_size(list, 1170, 480);
}
void accept(const std::string &value) {
  auto fn = chosen;
  close();
  if (fn)
    fn(value);
}
void request(const std::string &path) {
  folder = path;
  ++generation;
  lv_obj_clean(list);
  lv_label_set_text(caption,
                    ("Selecionar arquivo: " + path +
                     "\nLeitura em segundo plano; original permanece no SD.")
                        .c_str());
  auto *job = new Job{generation, path, filters};
  if (xQueueSend(jobs, &job, 0) != pdTRUE) {
    delete job;
    lv_label_set_text(
        caption, "Aguarde a leitura anterior e escolha a pasta novamente.");
  }
}
std::string get(cJSON *item, const char *key) {
  auto *v = cJSON_GetObjectItemCaseSensitive(item, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
} // namespace
void show_notice(lv_obj_t *parent,lv_group_t *group,const std::string &text) {
  open(parent,group,text,{});lv_obj_set_height(caption,220);
  button(modal,"Fechar",20,300,400,close);lv_obj_add_flag(list,LV_OBJ_FLAG_HIDDEN);
}
void confirm_action(lv_obj_t *parent, lv_group_t *group,
                    const std::string &text, std::function<void()> fn) {
  open(parent, group, text, [fn](const std::string &) { fn(); });
  lv_obj_set_height(caption, 220);
  button(modal, "Confirmar", 20, 300, 400, [] { accept("confirm"); });
  lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
}
void selectors_start() {
  jobs = xQueueCreate(2, sizeof(Job *));
  lock = xSemaphoreCreateMutex();
  xTaskCreate(worker, "import_list", 6144, nullptr, 1, nullptr);
}
void select_import_file(lv_obj_t *parent, lv_group_t *group,
                        const std::vector<std::string> &extensions,
                        std::function<void(const std::string &)> selected) {
  open(parent, group, "Arquivos no SD", std::move(selected));
  filters = extensions;
  const char *labels[] = {"Importacoes", "Uploads", "Downloads", "Backups"};
  for (unsigned i = 0; i < 4; i++)
    button(modal, labels[i], i * 220, 70, 210,
           [i] { request(import_roots()[i]); });
  button(modal, "Pasta acima", 890, 70, 240, [] {
    auto pos = folder.rfind('/');
    auto path = folder.substr(0, pos);
    if (import_path(path))
      request(path);
  });
  request(import_roots()[0]);
}
void selectors_tick() {
  std::unique_ptr<Result> next;
  xSemaphoreTake(lock, portMAX_DELAY);
  next = std::move(result);
  xSemaphoreGive(lock);
  if (!next || !modal || next->generation != generation)
    return;
  if (!next->error.empty()) {
    lv_label_set_text(caption, next->error.c_str());
    return;
  }
  unsigned index = 0;
  for (const auto &entry : next->entries)
    button(list, (entry.directory ? "[Pasta] " : "") + entry.name, 0,
           index++ * 50, 1100, [entry] {
             if (entry.directory)
               request(entry.path);
             else
               accept(entry.path);
           });
  if (!index) {
    auto *label = lv_label_create(list);
    lv_label_set_text(label, "Nenhum arquivo compativel. Copie .pem, .ppk ou "
                             ".conf para /TabRemoto/imports.");
  }
  if (index)
    lv_group_focus_obj(lv_obj_get_child(list, 0));
}
void select_saved_host(lv_obj_t *parent, lv_group_t *group,
                       const std::string &protocol,
                       std::function<void(const std::string &)> selected) {
  open(parent, group, "Selecionar host salvo", std::move(selected));
  auto state = security_status();
  if (!state.unlocked)
    button(modal, "Desbloquear cofre", 430, 65, 400, [] {
      close();
      security_ui_open();
    });
  auto *json = state.metadata ? cJSON_Parse(state.metadata->c_str()) : nullptr;
  auto *connections = cJSON_GetObjectItemCaseSensitive(json, "connections");
  unsigned index = 0;
  button(modal, "Conexao manual", 10, 65, 400, [] { accept(""); });
  for (auto *item = connections ? connections->child : nullptr; item;
       item = item->next) {
    if (get(item, "protocol") != protocol)
      continue;
    auto id = get(item, "id");
    auto text = get(item, "label") + " | " + get(item, "host");
    button(list, text, 0, index++ * 50, 1100, [id] { accept(id); });
  }
  if (!index) {
    auto *label = lv_label_create(list);
    lv_obj_set_width(label, 1080);
    lv_label_set_text(
        label,
        state.unlocked
            ? "Nenhum host deste protocolo salvo. Cadastre em Conexoes e cofre."
            : "Desbloqueie o cofre para visualizar e selecionar os hosts "
              "salvos.");
  }
  cJSON_Delete(json);
  lv_group_focus_obj(lv_obj_get_child(modal, 1));
}
void select_wifi_network(lv_obj_t *parent,lv_group_t *group,bool saved,std::function<void(const std::string&,bool)> fn){
 open(parent,group,saved?"Redes conhecidas | abertas: conectar apenas manualmente":"Redes encontradas | selecione a rede",{});
 unsigned row=0;
 if(saved){for(const auto &w:profiles_wifi()){
   auto name=w.ssid;bool secured=w.protected_network;
   button(list,name+(secured?" | senha salva":" | aberta, manual"),0,row++*56,1100,[name,secured,fn]{close();fn(name,secured);});
 }}else{
   auto *a=cJSON_Parse(network_status().wifi_scan_json.c_str());
   for(auto *item=a?a->child:nullptr;item;item=item->next){
    auto name=get(item,"ssid");if(name.empty())continue;bool secured=cJSON_IsTrue(cJSON_GetObjectItem(item,"protected"));
    auto *signal=cJSON_GetObjectItem(item,"rssi");
    button(list,name+(secured?" | protegida":" | aberta, manual")+" | "+std::to_string(signal?signal->valueint:0)+" dBm",0,row++*56,1100,[name,secured,fn]{close();fn(name,secured);});
   }cJSON_Delete(a);
 }
 if(!row){auto *label=lv_label_create(list);lv_label_set_text(label,saved?"Nenhuma rede salva. Conecte uma vez para salvar.":"Busca em andamento ou nenhuma rede encontrada. Feche e tente novamente.");lv_obj_set_width(label,1080);}
}
void select_rdp_resolution(lv_obj_t *parent, lv_group_t *group,
                           std::function<void(unsigned, unsigned)> fn) {
  open(parent, group,
       "Resolucao do desktop remoto | depende do servidor e da memoria "
       "disponivel",
       [fn](const std::string &value) {
         unsigned w = 0, h = 0;
         if (sscanf(value.c_str(), "%ux%u", &w, &h) == 2)
           fn(w, h);
       });
  unsigned row = 0;
  for (const auto &size : {"1280x720", "960x540", "800x450"}) {
    std::string text = size;
    button(list, text, 0, row++ * 70, 1080, [text] { accept(text); });
  }
}
void select_network_route(lv_obj_t *parent,lv_group_t *group,std::function<void(const std::string&,const std::string&)> fn) {
  open(parent,group,"Rota da conexao | escolha um perfil para redes sobrepostas",{});
  unsigned row=0;
  for(const auto &name:{"auto","wifi","ethernet"}) {
    std::string transport=name;
    button(list,transport,0,row++*56,1100,[fn,transport]{close();fn(transport,"");});
  }
  for(const auto &v:network_status().tunnels) {
    auto id=v.id,transport=v.transport;
    button(list,"VPN | "+id+" | "+transport+(v.up?" | conectada":" | indisponivel"),0,row++*56,1100,[fn,id,transport]{close();fn(transport,id);});
  }
}
void select_active_session(lv_obj_t *parent, lv_group_t *group) {
  open(parent, group,
       "Sessoes abertas | Ctrl+Tab: proxima | Ctrl+Shift+Tab: anterior", {});
  button(modal, "Nova sessao SSH", 10, 65, 360, [] {
    close();
    ui_session_new(false);
  });
  button(modal, "Nova sessao RDP", 390, 65, 360, [] {
    close();
    ui_session_new(true);
  });
  button(modal, "Tela inicial", 770, 65, 360, [] {
    close();
    ui_home();
  });
  unsigned row = 0;
  for (const auto &tab : ssh_sessions()) {
    if (tab.status.phase == SshStatus::Idle)
      continue;
    auto text = "SSH #" + std::to_string(tab.id + 1) + " | " + tab.status.user +
                "@" + tab.status.host + " | " + tab.status.detail + " | " + (tab.status.vpn_profile.empty() ? tab.status.transport : "VPN: "+tab.status.vpn_profile);
    auto id = tab.id;
    button(list, text, 0, row++ * 56, 1100, [id] {
      close();
      ui_session_open(false, id);
    });
  }
  for (const auto &tab : rdp_sessions()) {
    if (tab.status.phase == RdpStatus::Idle)
      continue;
    auto text = "RDP #" + std::to_string(tab.id + 1) + " | " + tab.status.host +
                " | " + tab.status.detail + " | " + (tab.status.vpn_profile.empty() ? tab.status.transport : "VPN: "+tab.status.vpn_profile);
    auto id = tab.id;
    button(list, text, 0, row++ * 56, 1100, [id] {
      close();
      ui_session_open(true, id);
    });
  }
  if (!row) {
    auto *label = lv_label_create(list);
    lv_label_set_text(
        label, "Nenhuma sessao aberta. Use os botoes acima para conectar.");
  }
}
} // namespace tabremoto
