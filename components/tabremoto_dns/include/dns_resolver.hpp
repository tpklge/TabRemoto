#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
namespace tabremoto {
struct DnsAddress {
  int family = 0;
  std::array<unsigned char, 16> bytes{};
};
// Each lookup owns a separate c-ares channel/cache and an explicit DNS server.
// The binder runs before connect/send; false aborts without a fallback query.
// Call outside the crypto gate: the SDK entropy adapter acquires it as needed.
bool dns_resolve(const std::string &host, const std::string &servers,
                 const std::function<bool(int)> &bind,
                 std::vector<DnsAddress> &addresses, std::string &error,
                 const std::atomic<bool> *cancel = nullptr);
} // namespace tabremoto
