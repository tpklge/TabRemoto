#pragma once
#include <stdint.h>
#include "esp_vfs_eventfd.h"
typedef uint64_t eventfd_t;
// ESP-IDF eventfds are nonblocking and accept only EFD_SUPPORT_ISR as a flag.
#define EFD_NONBLOCK 0
#define EFD_CLOEXEC 0
