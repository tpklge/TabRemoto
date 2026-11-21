#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {bool running,sha256_ok,random_ok,tls_ok,hostname_rejected,untrusted_rejected;int error;uint32_t elapsed_ms,internal_before,internal_after;char version[48],protocol[24],cipher[80],detail[160];} rdp_port_result_t;
int rdp_port_probe_start(void);
rdp_port_result_t rdp_port_probe_status(void);
#ifdef __cplusplus
}
#endif
