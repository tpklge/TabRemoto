#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool crypto_gate_enter(void);
bool crypto_gate_enter_for(unsigned milliseconds);
void crypto_gate_leave(void);
#ifdef __cplusplus
}
#endif
