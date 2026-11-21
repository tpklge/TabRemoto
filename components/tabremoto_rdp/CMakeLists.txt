if(TABREMOTO_OPENSSL_BOOTSTRAP)
 idf_component_register(INCLUDE_DIRS "include" REQUIRES vfs fatfs pthread esp_timer)
 return()
endif()
set(RDP_BUILD "${CMAKE_CURRENT_LIST_DIR}/../../research/build-rdp-idf")
set(RDP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../research/FreeRDP")
if(NOT EXISTS "${RDP_BUILD}/libfreerdp/libfreerdp3.a")
 message(FATAL_ERROR "Build the pinned native FreeRDP with tools/probe_rdp.sh and cmake --build research/build-rdp-idf first")
endif()
set(CHANNEL_SOURCES
 "${RDP_SOURCE}/channels/cliprdr/client/cliprdr_main.c"
 "${RDP_SOURCE}/channels/cliprdr/client/cliprdr_format.c"
 "${RDP_SOURCE}/channels/cliprdr/cliprdr_common.c"
 "${RDP_SOURCE}/channels/drdynvc/client/drdynvc_main.c"
 "${RDP_SOURCE}/channels/disp/client/disp_main.c"
 "${RDP_SOURCE}/channels/disp/disp_common.c"
 "${RDP_SOURCE}/channels/client/generic_dynvc.c"
 "${RDP_SOURCE}/channels/client/addin.c")
idf_component_register(SRCS ${CHANNEL_SOURCES} "rdp.cpp" "unsupported_fs.c" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "${RDP_SOURCE}/include" "${RDP_SOURCE}/winpr/include" "${RDP_BUILD}/include" "${RDP_BUILD}/winpr/include" REQUIRES tabremoto_profiles tabremoto_network tabremoto_ssh tabremoto_rdp_port tabremoto_crypto_gate esp_timer esp_system vfs fatfs pthread WHOLE_ARCHIVE)
add_prebuilt_library(tabremoto_freerdp "${RDP_BUILD}/libfreerdp/libfreerdp3.a")
add_prebuilt_library(tabremoto_winpr "${RDP_BUILD}/winpr/libwinpr/libwinpr3.a")
target_link_libraries(${COMPONENT_LIB} PUBLIC "-Wl,--start-group" tabremoto_freerdp tabremoto_winpr "${CMAKE_CURRENT_LIST_DIR}/../../research/openssl-build/libssl.a" "${CMAKE_CURRENT_LIST_DIR}/../../research/openssl-build/libcrypto.a" "-Wl,--end-group")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# Match the pinned native build: newlib on ESP-IDF has POSIX threads, not C11 threads.h.
target_compile_definitions(${COMPONENT_LIB} PRIVATE __STDC_NO_THREADS__=1)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../tabremoto_rdp_port/compat")
# Only these pinned, built-in client channels are linked; no dynamic plugin loader.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WITH_CHANNELS=1)
