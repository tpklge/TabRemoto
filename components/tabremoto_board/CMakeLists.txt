idf_component_register(SRCS "board.cpp" INCLUDE_DIRS "." REQUIRES m5stack_tab5 esp_lvgl_port esp_timer esp_system esp_hw_support PRIV_REQUIRES spi_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
