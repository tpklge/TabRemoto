#pragma once
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/inet.h"
#else
#include <arpa/inet.h>
#endif
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace tabremoto {
struct NetworkIp {
  uint8_t family = 0;
  std::array<uint8_t, 16> bytes{};
  bool operator==(const NetworkIp &other) const {
    return family == other.family && bytes == other.bytes;
  }
  bool operator!=(const NetworkIp &other) const { return !(*this == other); }
  bool empty() const {
    for (auto b : bytes)
      if (b)
        return false;
    return true;
  }
};
struct NetworkPrefix {
  NetworkIp network;
  unsigned bits = 0;
  bool operator==(const NetworkPrefix &other) const {
    return bits == other.bits && network == other.network;
  }
};
inline bool network_ip_parse(const std::string &text, NetworkIp &ip) {
  ip = {};
  if (text.empty() || text.size() > 45 || text.find('\0') != std::string::npos)
    return false;
  if (inet_pton(AF_INET, text.c_str(), ip.bytes.data()) == 1) {
    ip.family = 4;
    return true;
  }
  if (inet_pton(AF_INET6, text.c_str(), ip.bytes.data()) == 1) {
    ip.family = 6;
    return true;
  }
  ip = {};
  return false;
}
inline std::string network_ip_text(const NetworkIp &ip) {
  char buf[46]{};
  if (ip.family != 4 && ip.family != 6)
    return {};
  return inet_ntop(ip.family == 4 ? AF_INET : AF_INET6, ip.bytes.data(), buf,
                   sizeof(buf))
             ? buf
             : "";
}
inline NetworkIp network_prefix_mask(const NetworkPrefix &prefix) {
  NetworkIp mask;
  mask.family = prefix.network.family;
  for (unsigned i = 0; i < std::min(prefix.bits, prefix.network.family == 4 ? 32u : 128u); ++i)
    mask.bytes[i / 8] |= uint8_t(0x80 >> (i % 8));
  return mask;
}
inline bool network_prefix_parse(const std::string &text, NetworkPrefix &prefix,
                                 NetworkIp *host = nullptr) {
  auto slash = text.find('/');
  if (slash == std::string::npos ||
      text.find('/', slash + 1) != std::string::npos)
    return false;
  NetworkIp ip;
  if (!network_ip_parse(text.substr(0, slash), ip))
    return false;
  unsigned bits = 0;
  auto value = text.substr(slash + 1);
  if (value.empty() || value.size() > 3)
    return false;
  for (unsigned char c : value) {
    if (c < '0' || c > '9')
      return false;
    bits = bits * 10 + c - '0';
  }
  if (bits > (ip.family == 4 ? 32u : 128u))
    return false;
  if (host)
    *host = ip;
  prefix.network = ip;
  prefix.bits = bits;
  auto mask = network_prefix_mask(prefix);
  for (unsigned i = 0; i < 16; ++i)
    prefix.network.bytes[i] &= mask.bytes[i];
  return true;
}
inline bool network_prefix_contains(const NetworkPrefix &prefix,
                                    const NetworkIp &ip) {
  if (prefix.network.family != ip.family)
    return false;
  auto mask = network_prefix_mask(prefix);
  for (unsigned i = 0; i < 16; ++i)
    if ((ip.bytes[i] & mask.bytes[i]) != prefix.network.bytes[i])
      return false;
  return true;
}
struct NetworkRouteView {
  std::string profile;
  std::vector<NetworkPrefix> prefixes;
  bool armed = false, up = false;
};
struct NetworkRouteChoice {
  enum Result { Direct, Tunnel, Blocked, Ambiguous };
  Result result = Direct;
  int index = -1;
};
inline NetworkRouteChoice
network_route_choose(const std::vector<NetworkRouteView> &routes,
                     const NetworkIp &dest, const std::string &profile = "",
                     bool required = false) {
  if (!profile.empty()) {
    for (unsigned i = 0; i < routes.size(); ++i)
      if (routes[i].armed && routes[i].profile == profile) {
        for (const auto &prefix : routes[i].prefixes)
          if (network_prefix_contains(prefix, dest))
            return {routes[i].up ? NetworkRouteChoice::Tunnel
                                 : NetworkRouteChoice::Blocked,
                    int(i)};
        return {NetworkRouteChoice::Blocked, int(i)};
      }
    return {NetworkRouteChoice::Blocked, -1};
  }
  int best = -1, bits = -1;
  bool ambiguous = false;
  for (unsigned i = 0; i < routes.size(); ++i)
    if (routes[i].armed)
      for (const auto &prefix : routes[i].prefixes)
        if (network_prefix_contains(prefix, dest)) {
          if (int(prefix.bits) > bits) {
            best = i;
            bits = prefix.bits;
            ambiguous = false;
          } else if (int(prefix.bits) == bits && best != int(i))
            ambiguous = true;
        }
  if (ambiguous)
    return {NetworkRouteChoice::Ambiguous, -1};
  if (best < 0)
    return {required ? NetworkRouteChoice::Blocked : NetworkRouteChoice::Direct,
            -1};
  return {routes[best].up ? NetworkRouteChoice::Tunnel
                          : NetworkRouteChoice::Blocked,
          best};
}
} // namespace tabremoto
