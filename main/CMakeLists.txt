idf_component_register(SRCS "main.cpp" "provision.cpp" "files_diag.cpp" INCLUDE_DIRS "."
    REQUIRES tabremoto_profiles tabremoto_rdp tabremoto_network tabremoto_ssh tabremoto_security tabremoto_rdp_port tabremoto_board tabremoto_storage tabremoto_ui console json esp_app_format esp_system)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
