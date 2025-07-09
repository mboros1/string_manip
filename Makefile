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
# Only keep the essential flags in the default
CFLAGS ?= -O0 -flax-vector-conversions -g
TEST_FLAGS ?= -O0 -flax-vector-conversions -g

# If SIMDE_INCLUDE is set, add it to the flags
ifneq ($(SIMDE_INCLUDE),)
  CFLAGS += -I$(SIMDE_INCLUDE)
  TEST_FLAGS += -I$(SIMDE_INCLUDE)
endif

# Directories
SRC_DIR = .
OBJ_DIR = obj
BIN_DIR = bin

# Create necessary directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Source files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))

# Test files
TEST_FILES = $(wildcard $(SRC_DIR)/test_*.c)
TEST_TARGETS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(TEST_FILES))

# Utility targets
UTIL_TARGETS = generate_random_strings str_split_test str_len_test

# Default target
all: $(BIN_DIR) $(OBJ_DIR) $(UTIL_TARGETS) $(TEST_TARGETS)

# Compile each utility target
$(UTIL_TARGETS): %: $(OBJ_DIR)/%.o
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$@ $<

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build the test framework
$(OBJ_DIR)/faf_test.o: $(SRC_DIR)/faf_test.c $(SRC_DIR)/faf_test.h
	$(CC) $(TEST_FLAGS) -c $< -o $@

# Test framework dependency
test_framework: $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/faf_test.o

# Common component builds
$(OBJ_DIR)/faf_string.o: $(SRC_DIR)/faf_string.c $(SRC_DIR)/faf_string.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/faf_string_strlen.o: $(SRC_DIR)/faf_string_strlen.c $(SRC_DIR)/faf_string_strlen.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/faf_string_mem.o: $(SRC_DIR)/faf_string_mem.c $(SRC_DIR)/faf_string_mem.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/faf_string_cmp.o: $(SRC_DIR)/faf_string_cmp.c $(SRC_DIR)/faf_string_cmp.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/faf_string_strsplit.o: $(SRC_DIR)/faf_string_strsplit.c $(SRC_DIR)/faf_string_strsplit.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/faf_string_concat.o: $(SRC_DIR)/faf_string_concat.c $(SRC_DIR)/faf_string_concat.h
	$(CC) $(CFLAGS) -c $< -o $@

# Individual test build rules
$(BIN_DIR)/test_faf_string: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o -o $@

$(BIN_DIR)/test_faf_string_mem: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_mem.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_mem.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(BIN_DIR)/test_faf_string_strsplit: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_strsplit.o $(OBJ_DIR)/faf_string_strsplit.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_strsplit.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_strsplit.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(BIN_DIR)/test_faf_string_cmp: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_cmp.o $(OBJ_DIR)/faf_string_cmp.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_cmp.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_cmp.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(BIN_DIR)/test_faf_string_concat: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_concat.o $(OBJ_DIR)/faf_string_concat.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_concat.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_concat.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_toupper.o: $(SRC_DIR)/faf_string_toupper.c $(SRC_DIR)/faf_string_toupper.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_toupper: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_toupper.o $(OBJ_DIR)/faf_string_toupper.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_toupper.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_toupper.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_tolower.o: $(SRC_DIR)/faf_string_tolower.c $(SRC_DIR)/faf_string_tolower.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_tolower: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_tolower.o $(OBJ_DIR)/faf_string_tolower.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_tolower.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_tolower.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_trim.o: $(SRC_DIR)/faf_string_trim.c $(SRC_DIR)/faf_string_trim.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_trim: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_trim.o $(OBJ_DIR)/faf_string_trim.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_trim.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_trim.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_reverse.o: $(SRC_DIR)/faf_string_reverse.c $(SRC_DIR)/faf_string_reverse.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_reverse: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_reverse.o $(OBJ_DIR)/faf_string_reverse.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_reverse.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_reverse.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_contains.o: $(SRC_DIR)/faf_string_contains.c $(SRC_DIR)/faf_string_contains.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_contains: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_contains.o $(OBJ_DIR)/faf_string_contains.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_contains.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_contains.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_hash.o: $(SRC_DIR)/faf_string_hash.c $(SRC_DIR)/faf_string_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_hash: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_hash.o $(OBJ_DIR)/faf_string_hash.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_hash.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_hash.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(OBJ_DIR)/faf_string_sort.o: $(SRC_DIR)/faf_string_sort.c $(SRC_DIR)/faf_string_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string_sort: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_sort.o $(OBJ_DIR)/faf_string_sort.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_sort.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_sort.o $(OBJ_DIR)/faf_test.o -o $@ -g

# Target to run all tests using the framework
all_tests: test_framework $(TEST_TARGETS)
	@echo "All tests built with framework support"
	@for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		$$test; \
	done

# Target for individual test runs
test_%: $(BIN_DIR)/test_%
	@echo "Running $@..."
	@$(BIN_DIR)/$@

# Add targets for test explorer
test_explorer: $(BIN_DIR) $(OBJ_DIR) $(TEST_TARGETS)
	@echo "All tests built for test explorer"

# Show configuration
config:
	@echo "Current configuration:"
	@echo "CC        = $(CC)"
	@echo "CFLAGS    = $(CFLAGS)"
	@echo "TEST_FLAGS = $(TEST_FLAGS)"

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean all_tests test_framework test_explorer config

