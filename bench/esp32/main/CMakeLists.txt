set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

file(GLOB LIB_SRCS ${ROOT}/src/*.c ${ROOT}/src/kernels/*.c)
# The io group needs a file system and is left out (FAF_BENCH_HAVE_FILES)
set(BENCH_SRCS ${ROOT}/bench/bench_faf_string.c ${ROOT}/bench/bench_common.c
               ${ROOT}/bench/bench_alloc.c ${ROOT}/bench/bench_batch.c)

idf_component_register(SRCS main.c ${LIB_SRCS} ${BENCH_SRCS}
                       INCLUDE_DIRS ${ROOT}/src ${ROOT}/bench
                       PRIV_REQUIRES esp_hw_support
                       LDFRAGMENTS linker.lf)

# bench_faf_string.c's main() becomes bench_main(), called from app_main()
set_source_files_properties(${ROOT}/bench/bench_faf_string.c
                            PROPERTIES COMPILE_DEFINITIONS "main=bench_main")

# 16 KB regions like the host default (4 pools of 4096 slots of 4 bytes, the
# 32-bit SWAR slot size), and inputs sized for a few hundred KB of RAM
target_compile_definitions(${COMPONENT_LIB} PRIVATE
                           FAF_NPOOLS=4 FAF_POOL_SLOTS=4096
                           NLINES=400 BENCH_BIG=8192)

# Only some groups: idf.py -DFAF_BENCH_GROUPS="kernels" (tools/esp32_run.py)
if(DEFINED FAF_BENCH_GROUPS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
                             FAF_BENCH_GROUPS="${FAF_BENCH_GROUPS}")
endif()

# Extra definitions: idf.py -DFAF_DEFINES="FAF_ARENAS=0" (tools/esp32_run.py,
# make ESP32_DEFINES=...)
if(FAF_DEFINES)
  separate_arguments(FAF_DEFINE_LIST UNIX_COMMAND "${FAF_DEFINES}")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ${FAF_DEFINE_LIST})
endif()
