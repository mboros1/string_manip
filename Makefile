# Only include the .env file if it exists
ifneq ("$(wildcard .env)","")
  include .env
  # Make sure these variables are exported to commands
  export CC
  export SIMDE_INCLUDE
endif

# Define the compiler and options with defaults that can be overridden
CC ?= gcc
SIMDE_INCLUDE ?=
CFLAGS ?= -O0 -g
TEST_FLAGS ?= -O0 -g
# Benchmarks are always optimized, independent of CFLAGS
BENCH_FLAGS ?= -O2
# Appended to every library/test compile and link, e.g.
#   EXTRA_FLAGS=-DFAF_BACKEND_REF      force the scalar backend
#   EXTRA_FLAGS="-arch x86_64"         SSE2 backend on Apple Silicon (Rosetta)
EXTRA_FLAGS ?=

# simde is only used by the standalone experiments, not the library
UTIL_FLAGS = $(CFLAGS) -flax-vector-conversions
ifneq ($(SIMDE_INCLUDE),)
  UTIL_FLAGS += -I$(SIMDE_INCLUDE)
endif

# Directories
SRC_DIR = src
KERNEL_DIR = $(SRC_DIR)/kernels
TEST_DIR = tests
BENCH_DIR = bench
TOOLS_DIR = tools
EXP_DIR = experiments
OBJ_DIR = obj
BIN_DIR = bin

# The library needs no include paths; everything else includes it via src/
INCLUDES = -I$(SRC_DIR)

# Sources are found by name in these directories; objects all go in OBJ_DIR
vpath %.c $(SRC_DIR) $(KERNEL_DIR) $(TEST_DIR) $(TOOLS_DIR)

# Library
LIB_SRCS = $(KERNEL_DIR)/faf_kernels_ref.c $(KERNEL_DIR)/faf_kernels_simd.c \
           $(KERNEL_DIR)/faf_kernels_swar.c \
           $(addprefix $(SRC_DIR)/, \
             faf_string.c faf_string_strlen.c faf_string_mem.c \
             faf_string_cmp.c faf_string_concat.c faf_string_strsplit.c \
             faf_string_case.c faf_string_search.c faf_string_view.c \
             faf_string_build.c faf_string_parse.c faf_string_hash.c \
             faf_string_sort.c)
LIB_HEADERS = $(wildcard $(SRC_DIR)/*.h $(KERNEL_DIR)/*.h)
LIB_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(LIB_SRCS)))
LIB = $(OBJ_DIR)/libfaf.a

# Test files
TEST_FILES = $(wildcard $(TEST_DIR)/test_*.c)
TEST_TARGETS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TEST_FILES))

# Standalone experiments that are still built (need simde)
UTIL_TARGETS = str_split_test str_len_test

# Default target
all: $(LIB) $(UTIL_TARGETS) generate_random_strings $(TEST_TARGETS)

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Object files; -MMD -MP keeps header dependencies up to date
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR)/test_%.o: test_%.c | $(OBJ_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR)/faf_test.o: faf_test.c | $(OBJ_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

# Every test links its own object, the test framework and the library
$(BIN_DIR)/test_%: $(OBJ_DIR)/test_%.o $(OBJ_DIR)/faf_test.o $(LIB) | $(BIN_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) $^ -o $@

$(UTIL_TARGETS): %: $(EXP_DIR)/%.c | $(BIN_DIR)
	$(CC) $(UTIL_FLAGS) $< -o $(BIN_DIR)/$@

generate_random_strings: $(OBJ_DIR)/generate_random_strings.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) $^ -o $(BIN_DIR)/$@

# Benchmarks: library sources compiled together at BENCH_FLAGS.
# `make bench BENCH_GROUPS="alloc io"` runs only those groups.
BENCH_SRCS = $(addprefix $(BENCH_DIR)/, \
               bench_faf_string.c bench_common.c bench_alloc.c bench_io.c)
BENCH_GROUPS ?=
$(BIN_DIR)/bench_faf_string: $(BENCH_SRCS) $(BENCH_DIR)/bench.h $(LIB_SRCS) $(LIB_HEADERS) | $(BIN_DIR)
	$(CC) $(BENCH_FLAGS) $(EXTRA_FLAGS) $(INCLUDES) $(BENCH_SRCS) $(LIB_SRCS) -o $@

bench: $(BIN_DIR)/bench_faf_string
	@$(BIN_DIR)/bench_faf_string $(BENCH_GROUPS)

# Test framework dependency
test_framework: $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/faf_test.o

# Build and run all tests; fails if any test fails
all_tests: test_framework $(TEST_TARGETS)
	@echo "All tests built with framework support"
	@for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done

test: all_tests

# Target for individual test runs
test_%: $(BIN_DIR)/test_%
	@echo "Running $@..."
	@$(BIN_DIR)/$@

# Run the tests again on the word-at-a-time and byte-at-a-time backends
check_backends:
	$(MAKE) OBJ_DIR=$(OBJ_DIR)/swar BIN_DIR=$(BIN_DIR)/swar \
		EXTRA_FLAGS="$(EXTRA_FLAGS) -DFAF_BACKEND_SWAR" all_tests
	$(MAKE) OBJ_DIR=$(OBJ_DIR)/ref BIN_DIR=$(BIN_DIR)/ref \
		EXTRA_FLAGS="$(EXTRA_FLAGS) -DFAF_BACKEND_REF" all_tests

# Run the tests again with small pools, like a microcontroller build. Tests
# that need more room than that skip themselves (TEST_REQUIRE); none may fail.
SMALL_POOLS ?= -DFAF_NPOOLS=2 -DFAF_POOL_SLOTS=64
check_small:
	$(MAKE) OBJ_DIR=$(OBJ_DIR)/small BIN_DIR=$(BIN_DIR)/small \
		EXTRA_FLAGS="$(EXTRA_FLAGS) $(SMALL_POOLS)" all_tests

# Build the tests (or benchmarks) for an ESP32, flash them and collect the
# output over serial (tests/esp32/, bench/esp32/, tools/esp32_run.py). Needs
# ESP-IDF; IDF_TARGET picks the chip (esp32, esp32s3, ...) and ESPPORT the
# serial port when more than one is connected.
IDF_PATH ?= $(HOME)/esp/esp-idf-v6.1
IDF_TARGET ?= esp32
ESPPORT ?=
ESP32_RUN = bash -c '. "$(IDF_PATH)/export.sh" >/dev/null && \
	python tools/esp32_run.py --target $(IDF_TARGET) \
		$(if $(ESPPORT),--port $(ESPPORT)) "$$@"' esp32_run
esp32_test:
	@$(ESP32_RUN) --app=tests

esp32_bench:
	@$(ESP32_RUN) --app=bench

# Built freestanding, the library may import only memcpy, memset, memmove and
# memcmp, which GCC and Clang require from every environment, plus the
# compiler's own runtime helpers (libgcc/compiler-rt: software floating point
# and 64-bit division on small CPUs, e.g. __udivdi3, __divdf3). Apple targets
# also lower memset(p, 0, n) to bzero (__bzero on x86_64), which every Apple
# platform has. Works for cross compilers too, e.g.
#   make check_freestanding CC="xtensa-esp32-elf-gcc -mlongcalls"
FREESTANDING_DIR = $(OBJ_DIR)/freestanding
FREESTANDING_ALLOWED = memcpy memset memmove memcmp bzero __bzero
COMPILER_RUNTIME = ^__[a-z]+(qi|hi|si|di|ti|sf|df|tf|xf)[0-9]$$|^__(float|fix|extend|trunc)[a-z]+$$
check_freestanding:
	@mkdir -p $(FREESTANDING_DIR)
	@for f in $(LIB_SRCS); do \
		o=$$(basename $${f%.c}).o; \
		$(CC) -O2 -ffreestanding -fno-stack-protector $(EXTRA_FLAGS) \
			-c $$f -o $(FREESTANDING_DIR)/$$o || exit 1; \
	done
	@# Mach-O prefixes every C symbol with an underscore, ELF doesn't
	@lead=$$(nm -g --defined-only $(FREESTANDING_DIR)/faf_string.o | \
		awk '$$3 ~ /faf_string_init$$/ {sub(/faf_string_init$$/, "", $$3); print $$3; exit}'); \
	nm -g --defined-only $(FREESTANDING_DIR)/*.o | awk 'NF == 3 {print $$3}' | \
		sed "s/^$$lead//" | sort -u > $(FREESTANDING_DIR)/defined.txt; \
	nm -u $(FREESTANDING_DIR)/*.o | awk 'NF && $$NF !~ /:$$/ {print $$NF}' | \
		sed "s/^$$lead//" | sort -u > $(FREESTANDING_DIR)/undefined.txt
	@printf '%s\n' $(FREESTANDING_ALLOWED) | sort > $(FREESTANDING_DIR)/allowed.txt
	@comm -23 $(FREESTANDING_DIR)/undefined.txt $(FREESTANDING_DIR)/defined.txt | \
		comm -23 - $(FREESTANDING_DIR)/allowed.txt | \
		grep -vE '$(COMPILER_RUNTIME)' > $(FREESTANDING_DIR)/extra.txt || true
	@if [ -s $(FREESTANDING_DIR)/extra.txt ]; then \
		cat $(FREESTANDING_DIR)/extra.txt; \
		echo "library imports the symbols above; allowed: $(FREESTANDING_ALLOWED)"; \
		exit 1; \
	fi
	@n=$$(grep -cE '$(COMPILER_RUNTIME)' $(FREESTANDING_DIR)/undefined.txt); \
	echo "OK: library imports only $$(comm -12 $(FREESTANDING_DIR)/undefined.txt \
		$(FREESTANDING_DIR)/allowed.txt | tr '\n' ' ')$$([ $$n -gt 0 ] && \
		echo "+ $$n compiler runtime helpers")"

# Add targets for test explorer
test_explorer: $(BIN_DIR) $(OBJ_DIR) $(TEST_TARGETS)
	@echo "All tests built for test explorer"

# Show configuration
config:
	@echo "Current configuration:"
	@echo "CC          = $(CC)"
	@echo "CFLAGS      = $(CFLAGS)"
	@echo "TEST_FLAGS  = $(TEST_FLAGS)"
	@echo "BENCH_FLAGS = $(BENCH_FLAGS)"
	@echo "EXTRA_FLAGS = $(EXTRA_FLAGS)"

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

-include $(wildcard $(OBJ_DIR)/*.d)

.PHONY: all clean all_tests test test_framework test_explorer config bench \
        check_backends check_small check_freestanding esp32_test esp32_bench \
        generate_random_strings \
        $(UTIL_TARGETS)
