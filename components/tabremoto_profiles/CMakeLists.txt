idf_component_register(SRCS "profiles.cpp" INCLUDE_DIRS "include" REQUIRES tabremoto_storage tabremoto_crypto_gate nvs_flash mbedtls json bootloader_support esp_system)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
