idf_component_register(SRCS "storage.cpp" INCLUDE_DIRS "." REQUIRES m5stack_tab5 json PRIV_REQUIRES mbedtls sdmmc)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
