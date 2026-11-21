idf_component_register(SRCS "vault.cpp" "runtime.cpp" INCLUDE_DIRS "include"
    REQUIRES tabremoto_import tabremoto_rdp tabremoto_ssh tabremoto_network tabremoto_crypto_gate mbedtls json esp_timer bootloader_support esp_system tabremoto_storage tabremoto_rdp_port)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
