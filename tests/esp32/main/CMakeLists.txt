set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

file(GLOB LIB_SRCS ${ROOT}/src/*.c ${ROOT}/src/kernels/*.c)
file(GLOB TEST_SRCS ${ROOT}/tests/test_*.c)
list(SORT TEST_SRCS)

idf_component_register(SRCS main.c ${LIB_SRCS} ${TEST_SRCS} ${ROOT}/tests/faf_test.c
                       INCLUDE_DIRS ${ROOT}/src ${ROOT}/tests ${CMAKE_CURRENT_BINARY_DIR})

# Each test file has its own main(): rename it to <file>_main so main.c can
# run them all, and list them for main.c in test_list.h.
set(TEST_LIST "")
foreach(f ${TEST_SRCS})
  get_filename_component(name ${f} NAME_WE)
  set_source_files_properties(${f} PROPERTIES COMPILE_DEFINITIONS "main=${name}_main")
  string(APPEND TEST_LIST "TEST_FILE(${name})\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_list.h "${TEST_LIST}")

# Pools sized for a microcontroller: 4 pools of 2048 slots, 8 KB each with
# the 4-byte slots of the 32-bit SWAR backend. Tests that need bigger regions
# skip themselves (TEST_REQUIRE).
target_compile_definitions(${COMPONENT_LIB} PRIVATE FAF_NPOOLS=4 FAF_POOL_SLOTS=2048)

# Extra definitions: idf.py -DFAF_DEFINES="FAF_ARENAS=0" (tools/esp32_run.py,
# make ESP32_DEFINES=...)
if(FAF_DEFINES)
  separate_arguments(FAF_DEFINE_LIST UNIX_COMMAND "${FAF_DEFINES}")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ${FAF_DEFINE_LIST})
endif()
