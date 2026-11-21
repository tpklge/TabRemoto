idf_component_register(SRCS "ncm_driver.c" "ncm_descriptor.c" INCLUDE_DIRS "include" REQUIRES iot_usbh_cdc iot_eth esp_netif esp_event usb)
