/* NCM16 framing adapted from EspUsbHost 86b63f4 (TANAKA Masayuki), MIT.
 * Bounds are stricter: malformed blocks/CRC-mode and overlapping metadata fail.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline uint16_t ncm_read16(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t ncm_read32(const uint8_t *p) {
  return ncm_read16(p) | ((uint32_t)ncm_read16(p + 2) << 16);
}
static inline void ncm_write16(uint8_t *p, uint16_t n) {
  p[0] = n;
  p[1] = n >> 8;
}
static inline void ncm_write32(uint8_t *p, uint32_t n) {
  ncm_write16(p, n);
  ncm_write16(p + 2, n >> 16);
}
typedef void (*ncm_deliver_fn)(const uint8_t *, size_t, void *);
static inline int ncm_decode(const uint8_t *p, size_t size,
                             ncm_deliver_fn deliver, void *arg) {
  if (size < 12 || ncm_read32(p) != 0x484d434e || ncm_read16(p + 4) != 12)
    return -1;
  unsigned block = ncm_read16(p + 8), ndp = ncm_read16(p + 10), visited[8],
           count = 0, frames = 0;
  if (block != size || block < 12)
    return -1;
  while (ndp) {
    if (count == 8 || ndp < 12 || ndp + 8 > block)
      return -1;
    for (unsigned i = 0; i < count; i++)
      if (visited[i] == ndp)
        return -1;
    visited[count++] = ndp;
    if (ncm_read32(p + ndp) != 0x304d434e)
      return -1;
    unsigned length = ncm_read16(p + ndp + 4), next = ncm_read16(p + ndp + 6);
    if (length < 12 || length % 4 || ndp + length > block)
      return -1;
    int terminated = 0;
    for (unsigned off = 8; off + 4 <= length; off += 4) {
      unsigned at = ncm_read16(p + ndp + off),
               n = ncm_read16(p + ndp + off + 2);
      if (!at && !n) {
        terminated = 1;
        break;
      }
      if (at < 12 || n < 14 || n > 1514 || at + n > block ||
          (at < ndp + length && at + n > ndp))
        return -1;
      if (deliver) {
        deliver(p + at, n, arg);
      }
      ++frames;
    }
    if (!terminated)
      return -1;
    ndp = next;
  }
  return frames;
}
static inline size_t ncm_encode(uint8_t *out, size_t capacity,
                                const uint8_t *frame, size_t length,
                                uint16_t seq, unsigned divisor,
                                unsigned remainder, unsigned alignment) {
  if (!divisor || divisor > 64 || remainder >= divisor || !alignment ||
      alignment > 64 || length < 14 || length > 1514)
    return 0;
  unsigned ndp = (12 + alignment - 1) / alignment * alignment;
  unsigned data = ndp + 16;
  while (data % divisor != remainder)
    ++data;
  size_t total = data + length;
  if (total > capacity || total > 65535)
    return 0;
  memset(out, 0, data);
  ncm_write32(out, 0x484d434e);
  ncm_write16(out + 4, 12);
  ncm_write16(out + 6, seq);
  ncm_write16(out + 8, total);
  ncm_write16(out + 10, ndp);
  ncm_write32(out + ndp, 0x304d434e);
  ncm_write16(out + ndp + 4, 16);
  ncm_write16(out + ndp + 8, data);
  ncm_write16(out + ndp + 10, length);
  memcpy(out + data, frame, length);
  return total;
}
