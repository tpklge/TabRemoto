// Compile/link probe only: never announces a working tunnel or Ethernet link.
#include "esp_wireguard.h"
#include "iot_usbh_ecm.h"
// Volatile roots force protocol implementations into the ELF, even without
// a server attached. A successful compile alone never proves networking.
static esp_err_t (*volatile wg_entry)(wireguard_config_t*,wireguard_ctx_t*)=esp_wireguard_init;
static esp_err_t (*volatile wg_connect)(wireguard_ctx_t*)=esp_wireguard_connect;
static esp_err_t (*volatile wg_disconnect)(wireguard_ctx_t*)=esp_wireguard_disconnect;
static esp_err_t (*volatile ecm_entry)(const iot_usbh_ecm_config_t*,iot_eth_driver_t**)=iot_eth_new_usb_ecm;
void app_main(void) {
    wireguard_config_t config = ESP_WIREGUARD_CONFIG_DEFAULT();
    (void)config;
    (void)wg_entry;
    (void)wg_connect;
    (void)wg_disconnect;
    (void)ecm_entry;
}
