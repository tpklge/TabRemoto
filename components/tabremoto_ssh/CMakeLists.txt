set(SSH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../research/libssh2")
file(GLOB SSH_SOURCES "${SSH_ROOT}/src/*.c")
list(FILTER SSH_SOURCES EXCLUDE REGEX "/(agent|agent_win|libgcrypt|mbedtls|os400qc3|wincng|blowfish)\\.c$")
idf_component_register(SRCS "ssh.cpp" ${SSH_SOURCES} INCLUDE_DIRS "include" "${SSH_ROOT}/include"
    PRIV_INCLUDE_DIRS "${SSH_ROOT}/src" "${CMAKE_CURRENT_LIST_DIR}/../tabremoto_ui" REQUIRES tabremoto_import tabremoto_putty tabremoto_terminal tabremoto_rdp_port tabremoto_crypto_gate tabremoto_network tabremoto_storage json)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H LIBSSH2_OPENSSL)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
