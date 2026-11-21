set(VTERM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../research/libvterm")
file(GLOB VTERM_SOURCES "${VTERM_ROOT}/src/*.c")
foreach(table DECdrawing uk)
  execute_process(COMMAND perl -CSD "${VTERM_ROOT}/tbl2inc_c.pl" "${VTERM_ROOT}/src/encoding/${table}.tbl"
    OUTPUT_FILE "${VTERM_ROOT}/src/encoding/${table}.inc" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
idf_component_register(SRCS "terminal.cpp" ${VTERM_SOURCES} INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "${VTERM_ROOT}/include" REQUIRES heap)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
