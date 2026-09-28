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

# simde is only used by the standalone experiment utilities, not the library
UTIL_FLAGS = $(CFLAGS) -flax-vector-conversions
ifneq ($(SIMDE_INCLUDE),)
  UTIL_FLAGS += -I$(SIMDE_INCLUDE)
endif

# Directories
SRC_DIR = .
OBJ_DIR = obj
BIN_DIR = bin

# Library
LIB_SRCS = faf_kernels_ref.c faf_kernels_simd.c faf_string.c \
           faf_string_strlen.c faf_string_mem.c faf_string_cmp.c \
           faf_string_concat.c faf_string_strsplit.c faf_string_case.c \
           faf_string_search.c faf_string_view.c faf_string_build.c \
           faf_string_parse.c faf_string_hash.c faf_string_sort.c
LIB_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(LIB_SRCS))
LIB = $(OBJ_DIR)/libfaf.a

# Test files
TEST_FILES = $(wildcard $(SRC_DIR)/test_*.c)
TEST_TARGETS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(TEST_FILES))

# Utility targets (standalone experiments, need simde)
UTIL_TARGETS = str_split_test str_len_test

# Default target
all: $(LIB) $(UTIL_TARGETS) generate_random_strings $(TEST_TARGETS)

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Object files; -MMD -MP keeps header dependencies up to date
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/test_%.o: $(SRC_DIR)/test_%.c | $(OBJ_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/faf_test.o: $(SRC_DIR)/faf_test.c | $(OBJ_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

# Every test links its own object, the test framework and the library
$(BIN_DIR)/test_%: $(OBJ_DIR)/test_%.o $(OBJ_DIR)/faf_test.o $(LIB) | $(BIN_DIR)
	$(CC) $(TEST_FLAGS) $(EXTRA_FLAGS) $^ -o $@

$(UTIL_TARGETS): %: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(UTIL_FLAGS) $< -o $(BIN_DIR)/$@

generate_random_strings: $(OBJ_DIR)/generate_random_strings.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) $^ -o $(BIN_DIR)/$@

# Benchmarks: library sources compiled together at BENCH_FLAGS
$(BIN_DIR)/bench_faf_string: bench_faf_string.c $(LIB_SRCS) $(wildcard $(SRC_DIR)/faf*.h) | $(BIN_DIR)
	$(CC) $(BENCH_FLAGS) $(EXTRA_FLAGS) bench_faf_string.c $(LIB_SRCS) -o $@

bench: $(BIN_DIR)/bench_faf_string
	@$(BIN_DIR)/bench_faf_string

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

# Run the tests again on the scalar reference backend
check_backends:
	$(MAKE) OBJ_DIR=$(OBJ_DIR)/ref BIN_DIR=$(BIN_DIR)/ref \
		EXTRA_FLAGS="$(EXTRA_FLAGS) -DFAF_BACKEND_REF" all_tests

# The optimized library must not import libc's memory/string functions
FREESTANDING_DIR = $(OBJ_DIR)/freestanding
check_freestanding:
	@mkdir -p $(FREESTANDING_DIR)
	@for f in $(LIB_SRCS); do \
		$(CC) -O2 $(EXTRA_FLAGS) -c $$f -o $(FREESTANDING_DIR)/$${f%.c}.o || exit 1; \
	done
	@if nm -u $(FREESTANDING_DIR)/*.o | awk '{print $$NF}' | \
		grep -E '^_?(memcpy|memset|memmove|memcmp|bzero|strlen)$$'; then \
		echo "library imports the libc functions above"; exit 1; \
	fi
	@echo "OK: library imports no libc memory/string functions"

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
        check_backends check_freestanding generate_random_strings $(UTIL_TARGETS)
