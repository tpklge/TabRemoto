/* FAT exposes neither POSIX advisory locks nor fd permission changes.
 * Explicit failures for optional WinPR file APIs; never report fake success.
 */
#include <errno.h>
#include <sys/types.h>
int flock(int fd, int operation) {
  (void)fd;
  (void)operation;
  errno = ENOTSUP;
  return -1;
}
int fchmod(int fd, mode_t mode) {
  (void)fd;
  (void)mode;
  errno = ENOTSUP;
  return -1;
}
#include "esp_netif.h"
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
ssize_t readlink(const char *path, char *buffer, size_t size) {
  (void)path;
  (void)buffer;
  (void)size;
  errno = ENOTSUP;
  return -1;
}
pid_t waitpid(pid_t child, int *status, int options) {
  (void)child;
  (void)status;
  (void)options;
  errno = ECHILD;
  return -1;
}
int gethostname(char *buffer, size_t size) {
  if (!buffer || !size) {
    errno = EINVAL;
    return -1;
  }
  const char *name = "TabRemoto";
  esp_netif_t *net = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (net) {
    const char *configured = NULL;
    if (esp_netif_get_hostname(net, &configured) == ESP_OK && configured)
      name = configured;
  }
  size_t length = strlen(name);
  if (length >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(buffer, name, length + 1);
  return 0;
}
