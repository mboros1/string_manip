# Define the compiler and options
CC = gcc-14
CFLAGS = -O0 -I$(shell brew --prefix simde)/include/ -flax-vector-conversions -g
TEST_FLAGS = -O0 -I$(shell brew --prefix simde)/include/ -flax-vector-conversions -g

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

# Targets
TARGETS = generate_random_strings str_split_test str_len_test

# Default target
all: $(BIN_DIR) $(OBJ_DIR) $(TARGETS)

# Compile each target
$(TARGETS): %: $(OBJ_DIR)/%.o
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$@ $<

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_faf_string: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o -o $@

$(BIN_DIR)/test_faf_string_mem: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_mem.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_mem.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(BIN_DIR)/test_faf_string_strsplit: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_strsplit.o $(OBJ_DIR)/faf_string_strsplit.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_strsplit.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_strsplit.o $(OBJ_DIR)/faf_test.o -o $@ -g

$(BIN_DIR)/test_faf_string_cmp: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_cmp.o $(OBJ_DIR)/faf_string_cmp.o $(OBJ_DIR)/faf_test.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_cmp.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_cmp.o $(OBJ_DIR)/faf_test.o -o $@ -g

# Build the test framework
$(OBJ_DIR)/faf_test.o: $(SRC_DIR)/faf_test.c $(SRC_DIR)/faf_test.h
	$(CC) $(TEST_FLAGS) -c $< -o $@

# Test framework dependency
test_framework: $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/faf_test.o

# Target to run tests using the framework
framework_tests: test_framework $(BIN_DIR)/test_faf_string_strsplit
	@echo "Running framework tests..."
	@$(BIN_DIR)/test_faf_string_strsplit

# Target to run all tests with the framework
all_framework_tests: test_framework test
	@echo "All tests built with framework support"

# Keep the existing test target but update to depend on the framework
test: test_framework $(BIN_DIR)/test_faf_string $(BIN_DIR)/test_faf_string_mem $(BIN_DIR)/test_faf_string_strsplit $(BIN_DIR)/test_faf_string_cmp

# Add targets for test explorer
test_explorer: $(BIN_DIR) $(OBJ_DIR) $(patsubst $(SRC_DIR)/test_%.c,$(BIN_DIR)/test_%,$(wildcard $(SRC_DIR)/test_*.c))
	@echo "All tests built for test explorer"

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean

