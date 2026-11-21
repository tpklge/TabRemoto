#include "crypto_gate.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
// A binary semaphore deliberately permits handoff from the UI submitter to
// the security worker. Blocking waiters prevent a busy RDP loop starving SSH
// or USB provisioning. No heap allocation and no thread ownership transfer.
static portMUX_TYPE init_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t storage;
static SemaphoreHandle_t gate;
static void initialize(void) {
  taskENTER_CRITICAL(&init_lock);
  if (!gate) {
    gate = xSemaphoreCreateBinaryStatic(&storage);
    xSemaphoreGive(gate);
  }
  taskEXIT_CRITICAL(&init_lock);
}
bool crypto_gate_enter_for(unsigned milliseconds) {
  initialize();
  TickType_t ticks = pdMS_TO_TICKS(milliseconds);
  if (milliseconds && !ticks)
    ticks = 1;
  return xSemaphoreTake(gate, ticks) == pdTRUE;
}
bool crypto_gate_enter(void) { return crypto_gate_enter_for(0); }
void crypto_gate_leave(void) { xSemaphoreGive(gate); }
