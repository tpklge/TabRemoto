#include "dns_resolver.hpp"
#include <ares.h>
#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/select.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "bootloader_random.h"
#include "crypto_gate.h"
#include "esp_random.h"
#include "lwip/if_api.h"
#undef if_nametoindex
extern "C" unsigned int if_nametoindex(const char *name) {return lwip_if_nametoindex(name); }
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// c-ares' only ESP port hook: real SDK entropy, no rand()/timestamp fallback.
extern "C" void tabremoto_dns_random_bytes(unsigned char *data, size_t size) {
  while (!crypto_gate_enter_for(100))
    vTaskDelay(1);
  bootloader_random_enable();
  esp_fill_random(data, size);
  bootloader_random_disable();
  crypto_gate_leave();
}
#endif
namespace tabremoto {
namespace {
struct Lookup {
  std::function<bool(int)> binder;
  std::map<int, unsigned> sockets;
  std::vector<DnsAddress> result;
  bool done = false;
  int status = ARES_ECONNREFUSED;
};
std::once_flag initialized;
int initialization = ARES_ENOTINITIALIZED;
void socket_state(void *raw, ares_socket_t fd, int read, int write) {
  auto &lookup = *static_cast<Lookup *>(raw);
  if (!read && !write)
    lookup.sockets.erase(fd);
  else
    lookup.sockets[fd] =
        (read ? ARES_FD_EVENT_READ : 0) | (write ? ARES_FD_EVENT_WRITE : 0);
}
int socket_configure(ares_socket_t fd, int, void *raw) {
  auto &lookup = *static_cast<Lookup *>(raw);
  return fd >= 0 && fd < FD_SETSIZE && lookup.binder && lookup.binder(fd)
             ? ARES_SUCCESS
             : -1;
}
void answer(void *raw, int status, int, ares_addrinfo *result) {
  auto &lookup = *static_cast<Lookup *>(raw);
  lookup.status = status;
  lookup.done = true;
  if (status == ARES_SUCCESS && result)
    for (auto *node = result->nodes; node && lookup.result.size() < 8;
         node = node->ai_next) {
      DnsAddress address;
      if (node->ai_family == AF_INET && node->ai_addr &&
          node->ai_addrlen >= sizeof(sockaddr_in)) {
        address.family = 4;
        memcpy(address.bytes.data(),
               &reinterpret_cast<sockaddr_in *>(node->ai_addr)->sin_addr, 4);
      } else if (node->ai_family == AF_INET6 && node->ai_addr &&
                 node->ai_addrlen >= sizeof(sockaddr_in6)) {
        address.family = 6;
        memcpy(address.bytes.data(),
               &reinterpret_cast<sockaddr_in6 *>(node->ai_addr)->sin6_addr, 16);
      } else
        continue;
      lookup.result.push_back(address);
    }
  if (result)
    ares_freeaddrinfo(result);
}
} // namespace
bool dns_resolve(const std::string &host, const std::string &servers,
                 const std::function<bool(int)> &bind,
                 std::vector<DnsAddress> &addresses, std::string &error,
                 const std::atomic<bool> *cancel) {
  addresses.clear();
  error.clear();
  if (host.empty() || host.size() > 253 ||
      host.find('\0') != std::string::npos || servers.empty() ||
      servers.size() > 256) {
    error = "Hostname ou servidor DNS invalido";
    return false;
  }
  for (unsigned char c : host)
    if (c < 33 || c > 126) {
      error = "Hostname DNS deve usar ASCII/IDNA";
      return false;
    }
  DnsAddress literal;
  if (inet_pton(AF_INET, host.c_str(), literal.bytes.data()) == 1) {
    literal.family = 4;
    addresses.push_back(literal);
    return true;
  }
  if (inet_pton(AF_INET6, host.c_str(), literal.bytes.data()) == 1) {
    literal.family = 6;
    addresses.push_back(literal);
    return true;
  }
  std::call_once(initialized,
                 [] { initialization = ares_library_init(ARES_LIB_INIT_ALL); });
  if (initialization != ARES_SUCCESS) {
    error = "Nao foi possivel iniciar o resolvedor DNS";
    return false;
  }
  Lookup lookup;
  lookup.binder = bind;
  ares_options options{};
  options.flags = ARES_FLAG_NOSEARCH | ARES_FLAG_NOALIASES;
  options.timeout = 1500;
  options.tries = 2;
  options.sock_state_cb = socket_state;
  options.sock_state_cb_data = &lookup;
  char lookups[] = "b";
  options.lookups = lookups;
#ifdef ESP_PLATFORM
  char config[] = "/sdcard/TabRemoto/config/dns.conf";
  options.resolvconf_path = config;
#endif
  int flags = ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES |
              ARES_OPT_SOCK_STATE_CB | ARES_OPT_LOOKUPS;
#ifdef ESP_PLATFORM
  flags |= ARES_OPT_RESOLVCONF;
#endif
  ares_channel_t *channel = nullptr;
  int status = ares_init_options(&channel, &options, flags);
  if (status != ARES_SUCCESS) {
    error = ares_strerror(status);
    return false;
  }
  ares_set_socket_configure_callback(channel, socket_configure, &lookup);
  status = ares_set_servers_ports_csv(channel, servers.c_str());
  if (status != ARES_SUCCESS) {
    ares_destroy(channel);
    error = "Configuracao de DNS invalida";
    return false;
  }
  ares_addrinfo_hints hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = ARES_AI_NOSORT | ARES_AI_NUMERICSERV;
  ares_getaddrinfo(channel, host.c_str(), nullptr, &hints, answer, &lookup);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!lookup.done && std::chrono::steady_clock::now() < deadline &&
         !(cancel && cancel->load())) {
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    int maximum = -1;
    for (auto [fd, events] : lookup.sockets) {
      if (fd < 0 || fd >= FD_SETSIZE)
        continue;
      if (events & ARES_FD_EVENT_READ)
        FD_SET(fd, &reads);
      if (events & ARES_FD_EVENT_WRITE)
        FD_SET(fd, &writes);
      if (fd > maximum)
        maximum = fd;
    }
    timeval wait{0, 100000};
    int ready = select(maximum + 1, &reads, &writes, nullptr, &wait);
    if (ready < 0 && errno != EINTR)
      break;
    std::vector<ares_fd_events_t> events;
    if (ready > 0)
      for (auto [fd, mask] : lookup.sockets) {
        (void)mask;
        unsigned active = (FD_ISSET(fd, &reads) ? ARES_FD_EVENT_READ : 0) |
                          (FD_ISSET(fd, &writes) ? ARES_FD_EVENT_WRITE : 0);
        if (active)
          events.push_back({fd, active});
      }
    status = ares_process_fds(channel, events.data(), events.size(),
                              ARES_PROCESS_FLAG_NONE);
    if (status != ARES_SUCCESS)
      break;
  }
  if (!lookup.done) {
    ares_cancel(channel);
    error = cancel && cancel->load() ? "DNS cancelado"
                                     : "DNS sem resposta no perfil selecionado";
  }
  bool ok = lookup.done && lookup.status == ARES_SUCCESS &&
            !lookup.result.empty() && error.empty();
  if (ok)
    addresses = std::move(lookup.result);
  else if (error.empty())
    error = ares_strerror(lookup.status);
  ares_destroy(channel);
  return ok;
}
} // namespace tabremoto
