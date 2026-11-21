#pragma once
#include <string>
namespace tabremoto {
inline bool bounded_unsigned(const std::string &s, unsigned minimum,
                             unsigned maximum, unsigned &out) {
  if (s.empty()) return false;
  unsigned value = 0;
  for (unsigned char c : s) {
    if (c<'0' || c>'9') return false;
    unsigned digit = c-'0';
    if (digit>maximum || value>(maximum-digit)/10) return false;
    value=value*10+digit;
  }
  if (value<minimum) return false;
  out=value;return true;
}
}
