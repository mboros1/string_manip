set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

file(GLOB LIB_SRCS ${ROOT}/src/*.c ${ROOT}/src/kernels/*.c)
# The io group needs a file system and is left out (FAF_BENCH_HAVE_FILES)
set(BENCH_SRCS ${ROOT}/bench/bench_faf_string.c ${ROOT}/bench/bench_common.c
               ${ROOT}/bench/bench_alloc.c)

idf_component_register(SRCS main.c ${LIB_SRCS} ${BENCH_SRCS}
                       INCLUDE_DIRS ${ROOT}/src ${ROOT}/bench)

# bench_faf_string.c's main() becomes bench_main(), called from app_main()
set_source_files_properties(${ROOT}/bench/bench_faf_string.c
                            PROPERTIES COMPILE_DEFINITIONS "main=bench_main")

# 16 KB regions like the host default (4 x 2048 slots of 8 bytes), and
# inputs sized for a few hundred KB of RAM
target_compile_definitions(${COMPONENT_LIB} PRIVATE
                           FAF_NPOOLS=4 FAF_POOL_SLOTS=2048
                           NLINES=400 BENCH_BIG=8192)
