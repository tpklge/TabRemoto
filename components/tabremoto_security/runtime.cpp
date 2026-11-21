#include "cJSON.h"
#include "crypto_gate.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "imports.hpp"
#include "network.hpp"
#include "rdp.hpp"
#include "rdp_port.h"
#include "sdkconfig.h"
#include "security.hpp"
#include "esp_heap_caps.h"
#include "ssh.hpp"
#include "storage.hpp"
#include "vault.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace tabremoto {
SecurityRequest::~SecurityRequest() {
  secure_clear(password.data(), password.size());
  secure_clear(value.data(), value.size());
}
static SemaphoreHandle_t mutex;
static QueueHandle_t jobs;
static SecurityStatus state;
static std::atomic<bool> busy{false};
static std::atomic<int64_t> activity{0};
static void publish(SecurityStatus s) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  s.revision = state.revision + 1;
  state = std::move(s);
  xSemaphoreGive(mutex);
}
SecurityStatus security_status() {
  if (!mutex)
    return {};
  xSemaphoreTake(mutex, portMAX_DELAY);
  auto s = state;
  xSemaphoreGive(mutex);
  return s;
}
void security_activity() { activity = esp_timer_get_time(); }
bool security_busy() { return busy.load(); }
bool security_submit(SecurityRequest *r) {
  if (!r || !jobs || rdp_port_probe_status().running)
    return false;
  bool expected = false;
  if (!busy.compare_exchange_strong(expected, true))
    return false;
  if (xQueueSend(jobs, &r, 0) != pdTRUE) {
    busy = false;
    return false;
  }
  security_activity();
  return true;
}
#if CONFIG_TABREMOTO_DIAGNOSTICS
static bool selftest(std::string &error) {
  // This newlib's mkdtemp returns ENOSYS. mkdir itself is supported by VFS.
  char path[128];
  bool created = false;
  for (unsigned i = 0; i < 32; i++) {
    snprintf(path, sizeof(path), "/sdcard/TabRemoto/cache/security-%08x-%08x",
             (unsigned)esp_random(), (unsigned)esp_random());
    if (mkdir(path, 0700) == 0) {
      created = true;
      break;
    }
    if (errno != EEXIST)
      break;
  }
  if (!created) {
    error = std::string("Pasta exclusiva: ") + strerror(errno);
    return false;
  }
  bool ok = true;
  unsigned checks = 0;
  auto check = [&](bool condition, const char *message) {
    if (!condition) {
      ok = false;
      error = message;
    }
    auto s = security_status();
    s.message = "Autoteste: verificacao " + std::to_string(++checks);
    publish(s);
    return condition;
  };
  const std::string password = "Tab58221";
  const std::string secret_text = "TabRemoto public test secret";
  {
    Vault v(path);
    std::string secret, identity, connection;
    if (check(v.create(password), "Criacao/KDF") &&
        check(v.secret(secret, "Chave de teste", "key", secret_text),
              "Segredo AEAD") &&
        check(v.identity(identity, "Identidade teste", "test-user", "", secret,
                         "key"),
              "Identidade") &&
        check(v.connection(connection, "Favorito teste", "ssh", "LAB.Example.",
                           22, identity, true),
              "Conexao")) {
      check(!v.remove("identities", identity), "Referencia quebrada aceita");
      check(!v.remove("secrets", secret), "Segredo vinculado excluido");
      std::string fp(64, 'a'), changed(64, 'b');
      check(v.check_trust("ssh", "lab.example", 22, fp) == 0, "Trust inicial");
      check(v.trust("ssh", "lab.example", 22, fp, "",
                    "Fingerprint de teste conferido"),
            "Trust gravacao");
      check(v.check_trust("ssh", "LAB.EXAMPLE.", 22, fp) == 1,
            "Trust canonico");
      check(v.check_trust("ssh", "lab.example", 22, changed) == 2,
            "Chave alterada nao bloqueada");
      check(v.check_trust("rdp", "lab.example", 22, fp) == 0,
            "Trust cruzado entre protocolos");
      check(v.metadata().find(secret_text) == std::string::npos,
            "Segredo no export");
      v.lock();
      check(!v.unlock("Erro0000"), "Senha errada aceita");
      check(v.unlock(password), "Persistencia/desbloqueio");
      check(v.count("connections") == 1 && v.count("identities") == 1 &&
                v.count("secrets") == 1,
            "Dados perdidos");
      check(v.change_password("Nova8872"), "Troca de senha");
      v.lock();
      check(!v.unlock(password), "Senha antiga aceita na geracao atual");
      check(v.unlock("Nova8872"), "Nova senha rejeitada");
      // Payload authentication must reject a byte changed on the real SD.
      auto suffix = v.generation() % 2 ? "/store.a" : "/store.b";
      std::string current = std::string(path) + suffix;
      v.lock();
      FILE *f = fopen(current.c_str(), "r+b");
      if (check(f != nullptr, "Abrir ensaio adulteracao")) {
        fseek(f, -1, SEEK_END);
        int b = fgetc(f);
        fseek(f, -1, SEEK_END);
        fputc(b ^ 1, f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        check(!v.unlock("Nova8872"), "AEAD adulterado aceito");
        // The backup predates password rotation: it needs its old password.
        check(!v.unlock("Nova8872", true), "Backup antigo aceitou senha nova");
        check(v.unlock(password, true), "Recuperacao explicita falhou");
      }
    }
    if (ok && v.unlocked()) {
      const auto backup = std::string(path) + "/snapshot.trv";
      auto before = v.metadata();
      check(v.backup_encrypted(backup), "Backup SD cifrado");
      std::string temporary;
      check(v.secret(temporary, "Descartavel", "password", "Only-test-value"),
            "Preparar restauracao");
      auto changed = v.metadata();
      check(!v.restore_encrypted(backup, "Wrong123"),
            "Backup aceitou senha incorreta");
      check(v.metadata() == changed, "Senha incorreta alterou dados");
      FILE *file = fopen(backup.c_str(), "r+b");
      if (check(file != nullptr, "Abrir backup de ensaio")) {
        fseek(file, -1, SEEK_END);
        int byte = fgetc(file);
        fseek(file, -1, SEEK_END);
        fputc(byte ^ 1, file);
        fflush(file);
        fsync(fileno(file));
        check(!v.restore_encrypted(backup, password),
              "Backup adulterado aceito");
        check(v.metadata() == changed, "Adulteracao alterou o cofre");
        fseek(file, -1, SEEK_END);
        fputc(byte, file);
        fflush(file);
        fsync(fileno(file));
        fclose(file);
      }
      check(v.restore_encrypted(backup, password),
            "Restauracao SD autenticada");
      check(v.metadata() == before, "Restauracao perdeu cadastros");
      v.lock();
      check(v.unlock(password), "Senha atual apos restauracao");
      unlink(backup.c_str());
    }
    if (!ok && !v.error.empty())
      error += std::string(": ") + v.error;
  }
  if (!ok)
    error += std::string(" (ensaio SD/KDF)");
  unlink((std::string(path) + "/store.a").c_str());
  unlink((std::string(path) + "/store.b").c_str());
  unlink((std::string(path) + "/snapshot.trv").c_str());
  rmdir(path);
  return ok;
}
static bool boot_test(bool prepare, std::string &error) {
  std::string path = std::string(sd_root) + "/cache/security-boot-test";
  if (prepare && mkdir(path.c_str(), 0700) != 0) {
    error = "Pasta de teste existente preservada";
    return false;
  }
  Vault v(path);
  bool ok = false;
  if (prepare) {
    std::string secret, identity, connection;
    ok =
        v.create("Tab58221") &&
        v.secret(secret, "Chave persistencia", "key", "Public fake test key") &&
        v.identity(identity, "Identidade persistencia", "test-user", "", secret,
                   "key") &&
        v.connection(connection, "Favorito persistencia", "ssh", "lab.example",
                     22, identity, true) &&
        v.trust("ssh", "lab.example", 22, std::string(64, 'a'), "",
                "Fingerprint de laboratorio");
  } else {
    ok = v.unlock("Tab58221") && v.count("connections") == 1 &&
         v.count("identities") == 1 && v.count("secrets") == 1 &&
         v.count("trust") == 1 &&
         v.check_trust("ssh", "lab.example", 22, std::string(64, 'a')) == 1;
    v.lock();
    if (ok) {
      unlink((path + "/store.a").c_str());
      unlink((path + "/store.b").c_str());
      rmdir(path.c_str());
    }
  }
  if (!ok)
    error = v.error.empty() ? "Persistencia apos reboot falhou" : v.error;
  return ok;
}
#endif
static void worker(void *) {
  Vault vault(std::string(sd_root) + "/vault");
  SecurityStatus s;
  s.ready = true;
  s.exists = vault.exists();
  publish(s);
  unsigned failures = 0;
  unsigned unlocked_mount = 0;
  int64_t next_unlock = 0;
  for (;;) {
    SecurityRequest *raw = nullptr;
    if (xQueueReceive(jobs, &raw, pdMS_TO_TICKS(200)) == pdTRUE) {
      std::unique_ptr<SecurityRequest> r(raw);
      s.busy=true;s.message="Preparando operacao do cofre...";publish(s);
      bool kdf_job=r->action==SecurityAction::Create || r->action==SecurityAction::Unlock ||
       r->action==SecurityAction::Recover || r->action==SecurityAction::Password || r->action==SecurityAction::Restore || r->action==SecurityAction::Selftest || r->action==SecurityAction::PrepareBoot || r->action==SecurityAction::CheckBoot;
      bool loan=false;
      if(kdf_job && heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<16910000){
        s.message="Liberando memoria para o cofre; RDP sera retomado automaticamente";publish(s);
        loan=rdp_suspend_for_vault();
      }
      while (!crypto_gate_enter_for(100))vTaskDelay(1);
      auto start = esp_timer_get_time();
      std::string password = r->password.data();
      bool ok = false;
      bool locked_io = false;
      if (r->action == SecurityAction::Lock) {
        vault.lock();
        ok = true;
      } else if (esp_timer_get_time() < next_unlock &&
                 (r->action == SecurityAction::Unlock ||
                  r->action == SecurityAction::Recover))
        vault.error = "Aguarde antes de tentar novamente";
      else if (!(locked_io = storage_capture_begin()) ||
               !storage_snapshot().mounted)
        vault.error = "SD indisponivel";
      else {
        // Never carry an unlocked data key across a remount/card replacement.
        if (vault.unlocked() && storage_snapshot().generation != unlocked_mount)
          vault.lock();
        switch (r->action) {
        case SecurityAction::Create:
          ok = vault.create(password);
          break;
        case SecurityAction::Unlock:
        case SecurityAction::Recover:
          ok = vault.unlock(password, r->action == SecurityAction::Recover);
          if (!ok && vault.error.find("Memoria disponivel")!=0) {
            failures = std::min(failures + 1, 5u);
            next_unlock =
                esp_timer_get_time() + (int64_t(1u << failures) * 1000000);
          } else if(ok) {
            failures = 0;
            next_unlock = 0;
          }
          break;
        case SecurityAction::Password:
          ok = vault.change_password(password);
          break;
        case SecurityAction::Secret: {
          std::string value = r->value.data();
          ok = vault.secret(r->id, r->label, r->kind, value);
          secure_clear(value.data(), value.size());
          break;
        }
        case SecurityAction::SecretFile: {
          std::string value;
          ok = vault.unlocked() && import_read(r->file, value, vault.error);
          if (!vault.unlocked())
            vault.error = "Desbloqueie o cofre antes de importar";
          if (ok && r->kind == "key" &&
              value.find("PRIVATE KEY-----") == std::string::npos &&
              value.compare(0, 20, "PuTTY-User-Key-File-") != 0) {
            ok = false;
            vault.error = "Arquivo nao contem chave privada PEM/OpenSSH/PPK";
          }
          if (ok && r->kind != "key" && r->kind != "vpn") {
            ok = false;
            vault.error = "Tipo de importacao invalido";
          }
          if (ok)
            ok = vault.secret(r->id, r->label, r->kind, value);
          secure_clear(value.data(), value.size());
          break;
        }
        case SecurityAction::Identity:
          ok = vault.identity(r->id, r->label, r->user, r->domain, r->ref,
                              r->auth, r->host_ref);
          break;
        case SecurityAction::Connection:
          ok = vault.connection(r->id, r->label, r->protocol, r->host, r->port,
                                r->ref, r->favorite, r->group, r->transport,
                                r->vpn_ref, r->vpn_required);
          break;
        case SecurityAction::Remove:
          ok = vault.remove(r->collection, r->id);
          break;
        case SecurityAction::Trust:
          ok = vault.trust(r->protocol, r->host, r->port, r->fingerprint,
                           r->previous, r->reason);
          break;
        case SecurityAction::Import:
          ok = vault.import_catalog(r->value.data());
          break;
        case SecurityAction::Restore:
          if (!import_path(r->file)) {
            vault.error = "Backup fora das pastas permitidas";
            break;
          }
          ok = vault.restore_encrypted(r->file, r->password.data());
          break;
        case SecurityAction::Backup:
          ok = vault.backup_encrypted(
              std::string(sd_root) + "/backups/vault-g" +
              std::to_string(vault.generation()) + ".trv");
          break;
        case SecurityAction::Export: {
          auto text = vault.public_catalog();
          std::string path = std::string(sd_root) + "/config/connections-g" +
                             std::to_string(vault.generation()) + ".json";
          if (text.empty()) {
            vault.error = "Cofre bloqueado";
            break;
          }
          int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
          if (fd < 0) {
            vault.error = "Export existente ou SD indisponivel";
            break;
          }
          size_t offset = 0;
          while (offset < text.size()) {
            auto n = write(fd, text.data() + offset, text.size() - offset);
            if (n <= 0)
              break;
            offset += n;
          }
          ok = offset == text.size() && fsync(fd) == 0;
          if (close(fd) != 0)
            ok = false;
          if (!ok) {
            unlink(path.c_str());
            vault.error = "Falha na exportacao";
          }
          break;
        }
#if CONFIG_TABREMOTO_DIAGNOSTICS
        case SecurityAction::PrepareBoot:
        case SecurityAction::CheckBoot: {
          std::string message;
          ok = boot_test(r->action == SecurityAction::PrepareBoot, message);
          s.selftest = ok ? 2 : 3;
          if (!ok)
            vault.error = message;
          break;
        }
#endif
        case SecurityAction::Rdp: {
          vault.error.clear();
          auto *request = new RdpRequest;
          request->width = r->rdp_width;
          request->height = r->rdp_height;
          request->reconnect = r->rdp_reconnect;
          std::string kind, value, transport, vpn;
          bool required = false;
          int port = 3389;
          ok = vault.session_data(r->id, request->host, port, request->user,
                                  kind, value, request->fingerprint, transport,
                                  vpn, required, "rdp", &request->domain);
          request->port = port;
          request->transport=transport;
          request->vpn_profile=vpn;
          request->require_vpn=required || !vpn.empty();
          if(ok && !vpn.empty() && !network_vpn_active(vpn)) {
            std::string vpn_kind,vpn_value;
            ok=vault.secret_data(vpn,vpn_kind,vpn_value) && vpn_kind=="vpn" && network_vpn(vpn_value,transport,vpn);
            secure_clear(vpn_value.data(),vpn_value.size());
          }
          if (ok) {
            snprintf(request->password.data(), request->password.size(), "%s",
                     kind == "ask" ? r->password.data() : value.c_str());
            ok = rdp_connect(request);
          }
          secure_clear(value.data(), value.size());
          if (!ok) {
            delete request;
            if (vault.error.empty())
              vault.error = "Conexao RDP indisponivel";
          }
          break;
        }
        case SecurityAction::Ssh: {
          vault.error.clear();
          auto *request = new SshRequest;
          std::string kind, value;
          ok = vault.session_data(r->id, request->host, request->port,
                                  request->user, kind, value,
                                  request->fingerprint, request->transport,
                                  r->vpn_ref, request->require_vpn);
          if (!r->vpn_ref.empty()) {
            request->require_vpn = true;
            request->vpn_profile = r->vpn_ref;
          }
          if (ok && !r->vpn_ref.empty() && !network_vpn_active(r->vpn_ref)) {
            std::string vpn_kind, vpn_value;
            ok = vault.secret_data(r->vpn_ref, vpn_kind, vpn_value) &&
                 vpn_kind == "vpn" &&
                 network_vpn(vpn_value, request->transport, r->vpn_ref);
            secure_clear(vpn_value.data(), vpn_value.size());
          }
          if (ok) {
            if (kind == "key")
              request->key = value;
            else
              snprintf(request->password.data(), request->password.size(), "%s",
                       kind == "ask" ? r->password.data() : value.c_str());
            snprintf(request->passphrase.data(), request->passphrase.size(),
                     "%s", r->password.data());
            ok = ssh_connect(request);
          }
          if (!ok) {
            delete request;
            if (vault.error.empty())
              vault.error = "Conexao indisponivel ou identidade invalida";
          }
          secure_clear(value.data(), value.size());
          break;
        }
        case SecurityAction::Wifi:
        case SecurityAction::Vpn: {
          std::string kind, value;
          ok = vault.secret_data(r->id, kind, value);
          if (ok && r->action == SecurityAction::Vpn)
            ok = kind == "vpn" && network_vpn(value, r->protocol, r->id);
          else if (ok) {
            auto *j = cJSON_Parse(value.c_str());
            auto *ssid = cJSON_GetObjectItemCaseSensitive(j, "ssid");
            auto *pass = cJSON_GetObjectItemCaseSensitive(j, "password");
            ok = kind == "wifi" && cJSON_IsString(ssid) &&
                 cJSON_IsString(pass) &&
                 network_wifi(ssid->valuestring, pass->valuestring);
            if (cJSON_IsString(pass))
              secure_clear(pass->valuestring, strlen(pass->valuestring));
            cJSON_Delete(j);
          }
          secure_clear(value.data(), value.size());
          if (!ok)
            vault.error = "Perfil de rede invalido ou cofre bloqueado";
          break;
        }
#if CONFIG_TABREMOTO_DIAGNOSTICS
        case SecurityAction::Selftest: {
          std::string message;
          ok = selftest(message);
          s.selftest = ok ? 2 : 3;
          if (!ok)
            vault.error = message;
          break;
        }
#endif
        default:
          break;
        }
      }
      if (ok && vault.unlocked() &&
          (r->action == SecurityAction::Create ||
           r->action == SecurityAction::Unlock ||
           r->action == SecurityAction::Recover))
        unlocked_mount = storage_snapshot().generation;
      if (locked_io)
        storage_capture_end();
      s.message =
          ok ? (r->action == SecurityAction::Lock ? "Cofre bloqueado"
                                                  : "Salvo / verificado")
             : vault.error;
      s.operation_ms = (esp_timer_get_time() - start) / 1000;
      s.busy = false;
      s.exists = vault.exists();
      s.unlocked = vault.unlocked();
      s.generation = vault.generation();
      s.connections = vault.count("connections");
      s.identities = vault.count("identities");
      s.secrets = vault.count("secrets");
      s.trust = vault.count("trust");
      s.metadata = s.unlocked
                       ? std::make_shared<const std::string>(vault.metadata())
                       : nullptr;
      secure_clear(password.data(), password.size());
      r.reset();
      publish(s);
      busy = false;
      crypto_gate_leave();
      if(loan)rdp_resume_after_vault();
    }
    if (vault.unlocked() &&
        (!storage_snapshot().mounted ||
         storage_snapshot().generation != unlocked_mount ||
         esp_timer_get_time() - activity.load() > 300000000)) {
      vault.lock();
      s.unlocked = false;
      s.metadata.reset();
      s.generation = s.connections = s.identities = s.secrets = s.trust = 0;
      s.message = "Cofre bloqueado: inatividade ou SD alterado/ausente";
      publish(s);
    }
  }
}
void security_start() {
  mutex = xSemaphoreCreateMutex();
  jobs = xQueueCreate(1, sizeof(SecurityRequest *));
  security_activity();
  if (!mutex || !jobs ||
      xTaskCreatePinnedToCore(worker, "vault_worker", 24576, nullptr, 1,
                              nullptr, 1) != pdPASS) {
    SecurityStatus s;
    s.message = "Falha ao iniciar cofre";
    if (mutex)
      publish(s);
  }
}
} // namespace tabremoto
