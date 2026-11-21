idf_component_register(SRCS "selectors.cpp" "ui.cpp" "security_ui.cpp" "remote_ui.cpp" "rdp_ui.cpp" INCLUDE_DIRS "." REQUIRES tabremoto_profiles tabremoto_import tabremoto_rdp tabremoto_ssh tabremoto_network tabremoto_security json tabremoto_board tabremoto_storage esp_lvgl_port m5stack_tab5)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
