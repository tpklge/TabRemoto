#pragma once
#include <cstdint>
#include <vector>
namespace tabremoto {
struct NetworkCidr {
  uint32_t address, mask;
};
inline bool network_routes_overlap(const std::vector<NetworkCidr> &a,
                                   const std::vector<NetworkCidr> &b) {
  for (auto x : a)
    for (auto y : b)
      if ((x.address & y.mask) == y.address ||
          (y.address & x.mask) == x.address)
        return true;
  return false;
}
} // namespace tabremoto
