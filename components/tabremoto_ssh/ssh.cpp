#include "ssh.hpp"
#include "bootloader_random.h"
#include "cJSON.h"
#include "crypto_gate.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "imports.hpp"
#include "libssh2.h"
#include "libssh2_sftp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"
#include "network.hpp"
#include "openssl/evp.h"
#include "openssl/rand.h"
#include "putty_key.h"
#include "storage.hpp"
#include "terminal.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
// FATFS has no symbolic links; newlib does not expose lstat.
#define lstat stat
#endif
namespace tabremoto {
SshRequest::~SshRequest() {
  mbedtls_platform_zeroize(password.data(), password.size());
  mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
  mbedtls_platform_zeroize(key.data(), key.size());
}
namespace {
struct Session {
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t connects = nullptr, inputs = nullptr, files = nullptr;
  struct Input {
    std::string text;
    TerminalKey key = TerminalKey::None;
    unsigned modifiers = 0;
  };
  SshStatus state;
  std::atomic<bool> cancel{false};
  std::string accepted;
  std::atomic<bool> connecting{false}, local_list_busy{false};
  void publish(SshStatus s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    s.revision = state.revision + 1;
    s.local_listing = state.local_listing;
    state = std::move(s);
    xSemaphoreGive(mutex);
  }
  template <class F> auto crypto(F f) -> decltype(f()) {
    while (!crypto_gate_enter_for(100))
      vTaskDelay(1);
    bootloader_random_enable();
    auto result = f();
    bootloader_random_disable();
    crypto_gate_leave();
    return result;
  }
  int64_t now() { return esp_timer_get_time(); }
  void pause() { vTaskDelay(pdMS_TO_TICKS(5)); }
  template <class F> int retry(F f, int seconds = 20) {
    auto end = now() + int64_t(seconds) * 1000000;
    int result;
    do {
      if (cancel)
        return LIBSSH2_ERROR_SOCKET_DISCONNECT;
      result = crypto(f);
      if (result != LIBSSH2_ERROR_EAGAIN)
        return result;
      pause();
    } while (now() < end);
    return LIBSSH2_ERROR_TIMEOUT;
  }
  template <class T, class F> T *pointer_retry(LIBSSH2_SESSION *session, F f) {
    T *p = nullptr;
    auto end = now() + 20000000;
    while (!cancel && now() < end) {
      p = crypto(f);
      if (p || crypto([&] { return libssh2_session_last_errno(session); }) !=
                   LIBSSH2_ERROR_EAGAIN)
        break;
      pause();
    }
    return p;
  }
  Terminal terminal;
  void append(SshStatus &s, const char *p, size_t n) {
    terminal.feed(p, n);
    s.output = terminal.text();
    s.screen = terminal.frame();
    s.rx += n;
  }
  NetworkBinding connection;
  std::string connection_error;
  int socket_connect(const SshRequest &r) {
    if(r.require_vpn) {
      auto deadline=now()+30000000;
      while(!cancel && !network_vpn_active(r.vpn_profile) && now()<deadline) pause();
      if(!network_vpn_active(r.vpn_profile)) {connection_error="VPN selecionada indisponivel";return -1;}
    }
    connection_error.clear();
    return network_connect(r.host,r.port,r.transport,r.require_vpn,r.vpn_profile,connection,connection_error,&cancel);
  }
  bool send_data(LIBSSH2_CHANNEL *channel, const char *data, size_t n,
                 SshStatus &s) {
    auto end = now() + 30000000;
    while (n && !cancel && now() < end) {
      int rc =
          crypto([&] { return (int)libssh2_channel_write(channel, data, n); });
      if (rc == LIBSSH2_ERROR_EAGAIN) {
        pause();
        continue;
      }
      if (rc <= 0)
        return false;
      data += rc;
      n -= rc;
      s.tx += rc;
    }
    return n == 0;
  }
  bool safe_remote(const std::string &path) {
    return !path.empty() && path.size() <= 512 &&
           path.find('\n') == std::string::npos &&
           path.find('\r') == std::string::npos &&
           path.find('\0') == std::string::npos;
  }
  bool transfer(LIBSSH2_SESSION *session, const FileRequest &request,
                SshStatus &s) {
    if (!safe_remote(request.remote))
      return false;
    bool scp_download = request.operation == FileRequest::ScpDownload;
    auto *sftp =
        scp_download ? nullptr : pointer_retry<LIBSSH2_SFTP>(session, [&] {
          return libssh2_sftp_init(session);
        });
    if (!sftp && !scp_download)
      return false;
    bool ok = false;
    if (request.operation == FileRequest::Remove) {
      ok = retry([&] {
             return libssh2_sftp_unlink(sftp, request.remote.c_str());
           }) == 0;
    } else if (request.operation == FileRequest::List) {
      auto *handle = pointer_retry<LIBSSH2_SFTP_HANDLE>(session, [&] {
        return libssh2_sftp_opendir(sftp, request.remote.c_str());
      });
      auto *array = cJSON_CreateArray();
      if (handle) {
        char name[512];
        LIBSSH2_SFTP_ATTRIBUTES attr{};
        int rc;
        while ((rc = retry([&] {
                  return libssh2_sftp_readdir(handle, name, sizeof(name) - 1,
                                              &attr);
                })) > 0) {
          name[rc] = 0;
          if (cJSON_GetArraySize(array) >= 128)
            break;
          if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
          auto *item = cJSON_CreateObject();
          cJSON_AddStringToObject(item, "name", name);
          cJSON_AddBoolToObject(item, "directory",
                                (attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                                    LIBSSH2_SFTP_S_ISDIR(attr.permissions));
          cJSON_AddNumberToObject(item, "bytes", (double)attr.filesize);
          cJSON_AddItemToArray(array, item);
        }
        ok = rc >= 0;
        retry([&] { return libssh2_sftp_closedir(handle); });
      }
      char *encoded = cJSON_PrintUnformatted(array);
      s.listing = encoded ? encoded : "[]";
      cJSON_free(encoded);
      cJSON_Delete(array);
    } else {
      bool download = request.operation == FileRequest::Download ||
                      request.operation == FileRequest::ScpDownload;
      std::string local;
      if (!ssh_local_path(request.local, local)) {
        if (sftp)
          crypto([&] { return libssh2_sftp_shutdown(sftp); });
        return false;
      }
      // Pin the mount for the lifetime of every open file descriptor. The
      // storage worker must not unmount/remount FAT between network chunks.
      // All network calls remain cancellable and bounded in this worker.
      struct SdLease {
        bool held = storage_capture_begin();
        ~SdLease() {
          if (held)
            storage_capture_end();
        }
      } lease;
      if (!lease.held) {
        if (sftp)
          retry([&] { return libssh2_sftp_shutdown(sftp); });
        return false;
      }
      auto disk = storage_snapshot();
      if (!disk.mounted || (download && !disk.writable)) {
        if (sftp)
          retry([&] { return libssh2_sftp_shutdown(sftp); });
        return false;
      }
      int fd = -1;
      std::string temporary = local + ".part-XXXXXX";
      std::vector<char> name(temporary.begin(), temporary.end());
      name.push_back(0);
      fd = download ? mkstemp(name.data()) : open(local.c_str(), O_RDONLY);
      LIBSSH2_SFTP_HANDLE *handle = nullptr;
      LIBSSH2_CHANNEL *scp = nullptr;
      libssh2_struct_stat remote_stat{};
      uint64_t expected = 0, done = 0;
      std::string remote = request.remote;
      bool owns_remote_partial = false;
      if (!download) {
        char suffix[48];
        snprintf(suffix, sizeof(suffix), ".tabremoto-%08lx%08lx.part",
                 (unsigned long)esp_random(), (unsigned long)esp_random());
        remote += suffix;
      }
      bool use_scp = request.operation == FileRequest::ScpDownload ||
                     request.operation == FileRequest::ScpUpload;
      if (fd >= 0 && !download) {
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        int rc = retry([&] {
          return libssh2_sftp_stat(sftp, request.remote.c_str(), &attrs);
        });
        if (rc == 0 || crypto([&] { return libssh2_sftp_last_error(sftp); }) !=
                           LIBSSH2_FX_NO_SUCH_FILE) {
          close(fd);
          fd = -1;
        }
      }
      if (fd >= 0) {
        if (!download) {
          struct ::stat st{};
          if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
            expected = st.st_size;
          else {
            close(fd);
            fd = -1;
          }
        }
        if (fd >= 0) {
          if (use_scp) {
            if (download) {
              scp = pointer_retry<LIBSSH2_CHANNEL>(session, [&] {
                return libssh2_scp_recv2(session, request.remote.c_str(),
                                         &remote_stat);
              });
              expected = remote_stat.st_size;
            } else {
              auto *reservation =
                  pointer_retry<LIBSSH2_SFTP_HANDLE>(session, [&] {
                    return libssh2_sftp_open(sftp, remote.c_str(),
                                             LIBSSH2_FXF_WRITE |
                                                 LIBSSH2_FXF_CREAT |
                                                 LIBSSH2_FXF_EXCL,
                                             0600);
                  });
              if (reservation) {
                owns_remote_partial = true;
                retry([&] { return libssh2_sftp_close(reservation); });
              }
              if (owns_remote_partial)
                scp = pointer_retry<LIBSSH2_CHANNEL>(session, [&] {
                  return libssh2_scp_send64(session, remote.c_str(), 0600,
                                            expected, 0, 0);
                });
            }
          } else
            handle = pointer_retry<LIBSSH2_SFTP_HANDLE>(session, [&] {
              return libssh2_sftp_open(sftp, remote.c_str(),
                                       download ? LIBSSH2_FXF_READ
                                                : LIBSSH2_FXF_WRITE |
                                                      LIBSSH2_FXF_CREAT |
                                                      LIBSSH2_FXF_EXCL,
                                       0600);
            });
        }
      }
      if (!download && handle)
        owns_remote_partial = true;
      if (download && handle) {
        LIBSSH2_SFTP_ATTRIBUTES attributes{};
        if (retry([&] { return libssh2_sftp_fstat(handle, &attributes); }) ==
                0 &&
            (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE))
          expected = attributes.filesize;
      }
      s.transfer_bytes = 0;
      s.transfer_total = expected;
      publish(s);
      if (fd >= 0 && (handle || scp)) {
        char buffer[4096];
        auto deadline = now() + 300000000;
        ok = true;
        while (!cancel && now() < deadline) {
          int n = 0;
          if (download) {
            if (scp && done >= expected)
              break;
            n = retry([&] {
              return scp ? (int)libssh2_channel_read(
                               scp, buffer,
                               std::min<uint64_t>(sizeof(buffer),
                                                  expected - done))
                         : (int)libssh2_sftp_read(handle, buffer,
                                                  sizeof(buffer));
            });
            if (n == 0)
              break;
            if (n < 0) {
              ok = false;
              break;
            }
            int wrote = write(fd, buffer, n);
            if (wrote != n) {
              ok = false;
              break;
            }
            s.rx += n;
          } else {
            n = read(fd, buffer, sizeof(buffer));
            if (n == 0)
              break;
            if (n < 0) {
              ok = false;
              break;
            }
            size_t offset = 0;
            while (offset < (size_t)n) {
              int sent = retry([&] {
                return scp ? (int)libssh2_channel_write(scp, buffer + offset,
                                                        n - offset)
                           : (int)libssh2_sftp_write(handle, buffer + offset,
                                                     n - offset);
              });
              if (sent <= 0) {
                ok = false;
                break;
              }
              offset += sent;
              s.tx += sent;
            }
            if (!ok)
              break;
          }
          done += n;
          s.transfer_bytes = done;
          if (done > 64ULL * 1024 * 1024) {
            ok = false;
            break;
          }
          if (done % 65536 < (size_t)n) {
            s.detail = "Transferindo " + std::to_string(done) + " bytes";
            publish(s);
          }
        }
        if (cancel || now() >= deadline || (scp && done != expected))
          ok = false;
      }
      if (handle && retry([&] { return libssh2_sftp_close(handle); }) != 0)
        ok = false;
      if (scp) {
        retry([&] { return libssh2_channel_send_eof(scp); });
        retry([&] { return libssh2_channel_wait_closed(scp); });
        crypto([&] { return libssh2_channel_free(scp); });
      }
      if (!download && owns_remote_partial) {
        if (ok) {
          // SFTP v3 standard rename with flags=0: never request overwrite.
          ok = retry([&] {
                 return libssh2_sftp_rename_ex(
                     sftp, remote.c_str(), remote.size(),
                     request.remote.c_str(), request.remote.size(), 0);
               }) == 0;
        }
        if (!ok) {
          int removed =
              crypto([&] { return libssh2_sftp_unlink(sftp, remote.c_str()); });
          if (removed != 0)
            s.detail = "Arquivo parcial remoto: " + remote;
        }
      }
      if (fd >= 0) {
        {
          if (download && fsync(fd) != 0)
            ok = false;
          if (close(fd) != 0)
            ok = false;
          if (download && ok) {
            struct ::stat existing{};
            if (lstat(local.c_str(), &existing) == 0 ||
                rename(name.data(), local.c_str()) != 0)
              ok = false;
          }
          if (download && !ok)
            unlink(name.data());
        }
      }
    }
    if (sftp)
      retry([&] { return libssh2_sftp_shutdown(sftp); });
    return ok;
  }
  void run(SshRequest &r) {
    terminal = Terminal();
    SshStatus s;
    s.phase = SshStatus::Connecting;
    s.host = r.host;
    s.user = r.user;
    s.detail = "SSH: conexao TCP";
    publish(s);
    if (!r.key_file.empty()) {
      std::string error;
      bool lease = storage_capture_begin();
      bool ok = lease && import_read(r.key_file, r.key, error);
      if (lease)
        storage_capture_end();
      if (!ok) {
        s.phase = SshStatus::Failed;
        s.detail = error.empty() ? "Arquivo de chave indisponivel" : error;
        publish(s);
        return;
      }
    }
    if (r.key.compare(0, 20, "PuTTY-User-Key-File-") == 0) {
      char *converted = nullptr;
      size_t size = 0;
      const char *error = nullptr;
      bool ok = crypto([&] {
        return tabremoto_ppk_convert(r.key.c_str(), r.key.size(),
                                     r.passphrase.data(), &converted, &size,
                                     &error);
      });
      if (!ok) {
        s.phase = SshStatus::Failed;
        s.detail = error ? error : "Chave PPK invalida";
        publish(s);
        return;
      }
      mbedtls_platform_zeroize(r.key.data(), r.key.size());
      r.key.assign(converted, size);
      mbedtls_platform_zeroize(converted, size);
      free(converted);
      mbedtls_platform_zeroize(r.passphrase.data(), r.passphrase.size());
    }
    int fd = socket_connect(r);
    LIBSSH2_SESSION *session = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    bool success = false;

    if (fd < 0) {
      s.detail = connection_error.empty()?"Conexao TCP indisponivel":connection_error;
      goto done;
    }
    session = crypto([] {
      OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
      return libssh2_session_init();
    });
    if (!session) {
      s.detail = "Memoria insuficiente para SSH";
      goto done;
    }
    crypto([&] {
      libssh2_session_set_blocking(session, 0);
      libssh2_session_set_timeout(session, 15000);
      libssh2_session_method_pref(
          session, LIBSSH2_METHOD_HOSTKEY,
          "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256");
      libssh2_session_method_pref(
          session, LIBSSH2_METHOD_KEX,
          "curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256,"
          "diffie-hellman-group14-sha256");
      return 0;
    });
    if (retry([&] { return libssh2_session_handshake(session, fd); }) != 0) {
      s.detail = "Handshake SSH falhou";
      goto done;
    }
    {
      size_t length = 0;
      int type = 0;
      const char *hostkey = crypto(
          [&] { return libssh2_session_hostkey(session, &length, &type); });
      unsigned char digest[32];
      unsigned used = 0;
      if (!hostkey || !crypto([&] {
            return EVP_Digest(hostkey, length, digest, &used, EVP_sha256(),
                              nullptr);
          }) ||
          used != 32) {
        s.detail = "Fingerprint indisponivel";
        goto done;
      }
      char hex[65]{};
      for (unsigned i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
      s.fingerprint = hex;
      s.key_type = type == LIBSSH2_HOSTKEY_TYPE_ED25519 ? "ED25519"
                   : type == LIBSSH2_HOSTKEY_TYPE_RSA   ? "RSA"
                                                        : "ECDSA";
    }
    if (r.fingerprint != s.fingerprint) {
      s.phase = SshStatus::Verify;
      s.detail =
          r.fingerprint.empty()
              ? "Chave desconhecida: confirmar fingerprint antes de autenticar"
              : "CHAVE ALTERADA: autenticacao bloqueada";
      publish(s);
      auto end = now() + 120000000;
      bool approved = false;
      while (!cancel && now() < end) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        approved = accepted == s.fingerprint;
        xSemaphoreGive(mutex);
        if (approved)
          break;
        pause();
      }
      if (!approved) {
        s.detail = "Confianca nao confirmada";
        goto done;
      }
    }
    if (!network_connection_valid(connection)) {
      s.detail = "Rede mudou antes da autenticacao";
      goto done;
    }
    s.phase = SshStatus::Authenticating;
    s.detail = "Autenticando";
    publish(s);
    {
      int rc;
      if (r.key.empty())
        rc = retry([&] {
          return libssh2_userauth_password(session, r.user.c_str(),
                                           r.password.data());
        });
      else
        rc = retry([&] {
          return libssh2_userauth_publickey_frommemory(
              session, r.user.data(), r.user.size(), nullptr, 0, r.key.data(),
              r.key.size(), r.passphrase.data());
        });
      mbedtls_platform_zeroize(r.password.data(), r.password.size());
      mbedtls_platform_zeroize(r.passphrase.data(), r.passphrase.size());
      mbedtls_platform_zeroize(r.key.data(), r.key.size());
      r.key.clear();
      if (rc) {
        s.detail = "Autenticacao SSH recusada";
        goto done;
      }
    }
    channel = pointer_retry<LIBSSH2_CHANNEL>(
        session, [&] { return libssh2_channel_open_session(session); });
    if (!channel) {
      s.detail = "Canal SSH indisponivel";
      goto done;
    }
    if (r.command.empty()) {
      if (retry([&] {
            return libssh2_channel_request_pty_ex(channel, "xterm", 5, nullptr,
                                                  0, Terminal::columns,
                                                  Terminal::rows, 0, 0);
          }) != 0 ||
          retry([&] { return libssh2_channel_shell(channel); }) != 0) {
        s.detail = "Shell/PTY recusado";
        goto done;
      }
    } else if (retry([&] {
                 return libssh2_channel_exec(channel, r.command.c_str());
               }) != 0) {
      s.detail = "Comando recusado";
      goto done;
    }
    s.phase = SshStatus::Ready;
    s.detail = "SSH autenticado";
    s.transport=connection.transport;
    s.vpn_profile=connection.profile;
    success = true;
    publish(s);
    {
      char buffer[2048];
      int keepalive = 0;
      crypto([&] {
        libssh2_keepalive_config(session, 1, 15);
        return 0;
      });
      while (!cancel && network_connection_valid(connection)) {
        bool changed = false;
        for (int stream = 0; stream < 2; stream++) {
          int n = crypto([&] {
            return (int)libssh2_channel_read_ex(channel, stream, buffer,
                                                sizeof(buffer));
          });
          if (n > 0) {
            append(s, buffer, n);
            changed = true;
          } else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
            s.detail = "Canal SSH encerrado";
            cancel = true;
          }
        }
        auto response = terminal.replies();
        if (!response.empty())
          send_data(channel, response.data(), response.size(), s);
        Input *text = nullptr;
        while (xQueueReceive(inputs, &text, 0) == pdTRUE) {
          if (text->key != TerminalKey::None) {
            terminal.key(text->key, text->modifiers);
            auto bytes = terminal.replies();
            send_data(channel, bytes.data(), bytes.size(), s);
          } else
            send_data(channel, text->text.data(), text->text.size(), s);
          mbedtls_platform_zeroize(text->text.data(), text->text.size());
          delete text;
          changed = true;
        }
        FileRequest *job = nullptr;
        if (xQueueReceive(files, &job, 0) == pdTRUE) {
          s.transfer = true;
          s.transfer_ok = false;
          s.transfer_bytes = 0;
          s.transfer_total = 0;
          s.detail = job->operation == FileRequest::List
                         ? "Listando diretorio remoto"
                         : "Iniciando transferencia";
          publish(s);
          s.transfer_ok = transfer(session, *job, s);
          s.transfer = false;
          s.detail = s.transfer_ok ? "Transferencia/listagem concluida"
                     : s.detail.find("Arquivo parcial remoto:") == 0
                         ? "Transferencia falhou; " + s.detail
                         : "Transferencia falhou; sem sobrescrita automatica";
          delete job;
          changed = true;
        }
        if (changed)
          publish(s);
        crypto([&] { return libssh2_keepalive_send(session, &keepalive); });
        if (crypto([&] { return libssh2_channel_eof(channel); }))
          break;
        pause();
      }
    }
  done:
    if (channel) {
      retry([&] { return libssh2_channel_close(channel); }, 2);
      crypto([&] { return libssh2_channel_free(channel); });
    }
    if (session) {
      crypto([&] {
        return libssh2_session_disconnect(session, "TabRemoto encerrando");
      });
      crypto([&] { return libssh2_session_free(session); });
    }
    if (fd >= 0)
      close(fd);
    s.phase = success ? SshStatus::Closed : SshStatus::Failed;
    if (success)
      s.detail = "SSH desconectado | " + s.detail;
    publish(s);
  }
  void worker(void *) {
    for (;;) {
      SshRequest *raw = nullptr;
      if (xQueueReceive(connects, &raw, portMAX_DELAY) == pdTRUE) {
        {
          std::unique_ptr<SshRequest> r(raw);
          cancel = false;
          accepted.clear();
          run(*r);
        }
        Input *pending = nullptr;
        while (xQueueReceive(inputs, &pending, 0) == pdTRUE) {
          mbedtls_platform_zeroize(pending->text.data(), pending->text.size());
          delete pending;
        }
        FileRequest *file = nullptr;
        while (xQueueReceive(files, &file, 0) == pdTRUE)
          delete file;
        connecting = false;
      }
    }
  }
  SshStatus snapshot() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto s = state;
    xSemaphoreGive(mutex);
    if (files)
      s.queued = uxQueueMessagesWaiting(files);
    return s;
  }
};
std::array<std::atomic<Session *>, ssh_session_limit> sessions{};
std::atomic<unsigned> active{0};
SemaphoreHandle_t registry;
Session *ensure(unsigned id) {
  if (id >= sessions.size())
    return nullptr;
  xSemaphoreTake(registry, portMAX_DELAY);
  auto *s = sessions[id].load();
  if (!s && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 64 * 1024) {
    s = new Session;
    s->mutex = xSemaphoreCreateMutex();
    s->connects = xQueueCreate(1, sizeof(SshRequest *));
    s->inputs = xQueueCreate(32, sizeof(Session::Input *));
    s->files = xQueueCreate(4, sizeof(FileRequest *));
    TaskHandle_t task = nullptr;
    bool ok =
        s->mutex && s->connects && s->inputs && s->files &&
        xTaskCreatePinnedToCore(
            [](void *ctx) { static_cast<Session *>(ctx)->worker(nullptr); },
            "ssh_session", 24576, s, 1, &task, 1) == pdPASS;
    if (ok)
      sessions[id] = s;
    else {
      if (s->mutex)
        vSemaphoreDelete(s->mutex);
      if (s->connects)
        vQueueDelete(s->connects);
      if (s->inputs)
        vQueueDelete(s->inputs);
      if (s->files)
        vQueueDelete(s->files);
      delete s;
      s = nullptr;
    }
  }
  xSemaphoreGive(registry);
  return s;
}
Session &selected() { return *sessions[active.load()].load(); }
} // namespace
void ssh_start() {
  registry = xSemaphoreCreateMutex();
  auto *first = ensure(0);
  if (first)
    first->crypto([] { return libssh2_init(LIBSSH2_INIT_NO_CRYPTO); });
}
unsigned ssh_selected() { return active; }
bool ssh_select(unsigned id) {
  if (id >= sessions.size() || !sessions[id])
    return false;
  active = id;
  return true;
}
bool ssh_new_session() {
  for (unsigned id = 0; id < sessions.size(); ++id) {
    auto *s = sessions[id].load();
    if (!s || !s->connecting) {
      if (!ensure(id))
        return false;
      active = id;
      return true;
    }
  }
  return false;
}
std::vector<SshTab> ssh_sessions() {
  std::vector<SshTab> tabs;
  for (unsigned id = 0; id < sessions.size(); ++id)
    if (auto *s = sessions[id].load())
      tabs.push_back({id, s->snapshot()});
  return tabs;
}
bool ssh_connect(SshRequest *r) {
  if (!r || r->host.empty() || r->host.size() > 253 || r->user.empty() ||
      r->user.size() > 128 || r->port < 1 || r->port > 65535)
    return false;
  if (selected().connecting && !ssh_new_session())
    return false;
  auto &s = selected();
  bool expected = false;
  if (!s.connecting.compare_exchange_strong(expected, true))
    return false;
  if (xQueueSend(s.connects, &r, 0) != pdTRUE) {
    s.connecting = false;
    return false;
  }
  return true;
}
void ssh_disconnect() { selected().cancel = true; }
void ssh_disconnect_all() {
  for (auto &entry : sessions)
    if (auto *s = entry.load())
      s->cancel = true;
}
void ssh_cancel_files() { ssh_disconnect(); }
bool ssh_accept(const std::string &fingerprint) {
  auto &ctx = selected();
  auto s = ctx.snapshot();
  if (s.phase != SshStatus::Verify || fingerprint != s.fingerprint)
    return false;
  xSemaphoreTake(ctx.mutex, portMAX_DELAY);
  ctx.accepted = fingerprint;
  xSemaphoreGive(ctx.mutex);
  return true;
}
SshStatus ssh_status() {
  return sessions[active.load()] ? selected().snapshot() : SshStatus{};
}
bool ssh_input(const std::string &value) {
  auto &s = selected();
  if (s.snapshot().phase != SshStatus::Ready || value.empty() ||
      value.size() > 4096)
    return false;
  auto *data = new Session::Input{value};
  if (xQueueSend(s.inputs, &data, 0) != pdTRUE) {
    delete data;
    return false;
  }
  return true;
}
bool ssh_key(TerminalKey key, unsigned modifiers) {
  auto &s = selected();
  if (s.snapshot().phase != SshStatus::Ready || key == TerminalKey::None)
    return false;
  auto *data = new Session::Input{"", key, modifiers};
  if (xQueueSend(s.inputs, &data, 0) != pdTRUE) {
    delete data;
    return false;
  }
  return true;
}
bool ssh_files(const FileRequest &value) {
  auto &s = selected();
  if (s.snapshot().phase != SshStatus::Ready)
    return false;
  auto *r = new FileRequest(value);
  if (xQueueSend(s.files, &r, 0) != pdTRUE) {
    delete r;
    return false;
  }
  return true;
}
static void local_inventory_worker(void *raw) {
  auto &ctx = *static_cast<Session *>(raw);
  auto *array = cJSON_CreateArray();
  if (storage_capture_begin()) {
    for (const char *folder : {"downloads", "uploads"}) {
      auto base = std::string(sd_root) + "/transfers/" + folder;
      DIR *directory = opendir(base.c_str());
      if (directory) {
        for (unsigned i = 0; i < 128;) {
          auto *entry = readdir(directory);
          if (!entry)
            break;
          std::string name = entry->d_name;
          if (name == "." || name == "..")
            continue;
          struct stat info{};
          if (stat((base + "/" + name).c_str(), &info) != 0 ||
              !S_ISREG(info.st_mode))
            continue;
          auto *item = cJSON_CreateObject();
          cJSON_AddStringToObject(item, "name",
                                  (std::string(folder) + "/" + name).c_str());
          cJSON_AddNumberToObject(item, "bytes", info.st_size);
          cJSON_AddItemToArray(array, item);
          ++i;
        }
        closedir(directory);
      }
    }
    storage_capture_end();
  }
  auto *text = cJSON_PrintUnformatted(array);
  cJSON_Delete(array);
  xSemaphoreTake(ctx.mutex, portMAX_DELAY);
  ctx.state.local_listing = text ? text : "[]";
  ++ctx.state.revision;
  xSemaphoreGive(ctx.mutex);
  cJSON_free(text);
  ctx.local_list_busy = false;
  vTaskDelete(nullptr);
}
bool ssh_local_list() {
  auto &ctx = selected();
  if (ctx.local_list_busy.exchange(true))
    return false;
  if (xTaskCreate(local_inventory_worker, "local_files", 6144, &ctx, 1,
                  nullptr) != pdPASS) {
    ctx.local_list_busy = false;
    return false;
  }
  return true;
}
bool ssh_local_path(const std::string &name, std::string &path) {
  std::string folder = "downloads", basename = name;
  for (const char *candidate : {"downloads", "uploads"}) {
    std::string prefix = std::string(candidate) + "/";
    if (name.compare(0, prefix.size(), prefix) == 0) {
      folder = candidate;
      basename = name.substr(prefix.size());
    }
  }
  if (basename.empty() || basename.size() > 128 ||
      basename.find('/') != std::string::npos ||
      basename.find('\\') != std::string::npos || basename == "." ||
      basename == "..")
    return false;
  for (unsigned char c : name)
    if (c < 32 || c == 127)
      return false;
  path = std::string(sd_root) + "/transfers/" + folder + "/" + basename;
  struct ::stat st{};
  return lstat(path.c_str(), &st) != 0 || S_ISREG(st.st_mode);
}
} // namespace tabremoto
