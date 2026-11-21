#include "files_diag.hpp"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "ssh.hpp"
#include "storage.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>
using namespace tabremoto;
bool files_fixture(cJSON *reply) {
  if (!storage_capture_begin())
    return false;
  char path[] = "/sdcard/TabRemoto/transfers/downloads/TabRemoto-test-XXXXXX";
  int fd = mkstemp(path);
  const char payload[] = "TabRemoto SCP/SFTP real UTF-8: á é ç ü\n";
  bool ok = fd >= 0 &&
            write(fd, payload, sizeof(payload) - 1) == sizeof(payload) - 1 &&
            fsync(fd) == 0;
  if (fd >= 0)
    close(fd);
  if (ok)
    cJSON_AddStringToObject(reply, "name", strrchr(path, '/') + 1);
  else if (fd >= 0)
    unlink(path);
  storage_capture_end();
  return ok;
}
bool files_hash(const char *name, cJSON *reply) {
  std::string path;
  if (!ssh_local_path(name, path) || !storage_capture_begin())
    return false;
  FILE *f = fopen(path.c_str(), "rb");
  bool ok = f != nullptr;
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  ok = ok && mbedtls_sha256_starts(&ctx, 0) == 0;
  size_t count = 0, n;
  unsigned char data[1024], digest[32];
  if (f) {
    while (ok && (n = fread(data, 1, sizeof(data), f))) {
      count += n;
      if (count > 64ULL * 1024 * 1024) {
        ok = false;
        break;
      }
      ok = mbedtls_sha256_update(&ctx, data, n) == 0;
    }
    if (ferror(f))
      ok = false;
    fclose(f);
  }
  ok = ok && mbedtls_sha256_finish(&ctx, digest) == 0;
  mbedtls_sha256_free(&ctx);
  if (ok) {
    char hash[65]{};
    for (unsigned i = 0; i < 32; i++)
      snprintf(hash + i * 2, 3, "%02x", digest[i]);
    cJSON_AddStringToObject(reply, "sha256", hash);
    cJSON_AddNumberToObject(reply, "bytes", count);
  }
  storage_capture_end();
  return ok;
}
bool files_remove(const char *name) {
  std::string path;
  if (strncmp(name, "TabRemoto-test-", 15) || !ssh_local_path(name, path) ||
      !storage_capture_begin())
    return false;
  bool ok = unlink(path.c_str()) == 0;
  storage_capture_end();
  return ok;
}
