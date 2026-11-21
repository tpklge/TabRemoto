#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace tabremoto {
constexpr size_t clipboard_text_limit = 4096;
inline bool clipboard_utf16(const std::string &text,
                            std::vector<uint8_t> &out) {
  out.clear();
  if (text.size() > clipboard_text_limit)
    return false;
  auto put = [&](uint16_t u) {
    out.push_back(u & 255);
    out.push_back(u >> 8);
  };
  for (size_t i = 0; i < text.size();) {
    auto b = uint8_t(text[i++]);
    uint32_t cp = b;
    unsigned n = 0;
    uint32_t min = 0;
    if (!b)
      return false;
    if (b >= 0xc2 && b <= 0xdf) {
      cp = b & 31;
      n = 1;
      min = 0x80;
    } else if (b >= 0xe0 && b <= 0xef) {
      cp = b & 15;
      n = 2;
      min = 0x800;
    } else if (b >= 0xf0 && b <= 0xf4) {
      cp = b & 7;
      n = 3;
      min = 0x10000;
    } else if (b >= 0x80)
      return false;
    if (i + n > text.size())
      return false;
    for (unsigned j = 0; j < n; ++j) {
      auto c = uint8_t(text[i++]);
      if ((c & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (c & 63);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
    if (cp < 0x10000)
      put(cp);
    else {
      cp -= 0x10000;
      put(0xd800 + (cp >> 10));
      put(0xdc00 + (cp & 1023));
    }
  }
  put(0);
  return true;
}
inline bool clipboard_utf8(const uint8_t *data, size_t size, std::string &out) {
  out.clear();
  if (!data || size < 2 || size > clipboard_text_limit * 2 + 2 || size % 2)
    return false;
  auto get = [&](size_t i) {
    return uint16_t(data[i]) | (uint16_t(data[i + 1]) << 8);
  };
  bool terminated = false;
  for (size_t i = 0; i < size; i += 2) {
    uint32_t cp = get(i);
    if (!cp) {
      terminated = true;
      for (size_t j = i + 2; j < size; j += 2)
        if (get(j))
          return false;
      break;
    }
    if (cp >= 0xd800 && cp <= 0xdbff) {
      if (i + 3 >= size)
        return false;
      auto low = get(i + 2);
      if (low < 0xdc00 || low > 0xdfff)
        return false;
      cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
      i += 2;
    } else if (cp >= 0xdc00 && cp <= 0xdfff)
      return false;
    if (cp < 0x80)
      out += char(cp);
    else if (cp < 0x800) {
      out += char(0xc0 | (cp >> 6));
      out += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      out += char(0xe0 | (cp >> 12));
      out += char(0x80 | ((cp >> 6) & 63));
      out += char(0x80 | (cp & 63));
    } else {
      out += char(0xf0 | (cp >> 18));
      out += char(0x80 | ((cp >> 12) & 63));
      out += char(0x80 | ((cp >> 6) & 63));
      out += char(0x80 | (cp & 63));
    }
    if (out.size() > clipboard_text_limit)
      return false;
  }
  return terminated;
}
} // namespace tabremoto
