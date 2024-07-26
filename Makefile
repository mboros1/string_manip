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

$(BIN_DIR)/test_faf_string: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string.o $(OBJ_DIR)/faf_string_strlen.o -o $@

$(BIN_DIR)/test_faf_string_mem: $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/faf_string_strlen.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/test_faf_string_mem.o
	$(CC) $(TEST_FLAGS) $(OBJ_DIR)/faf_string.o $(OBJ_DIR)/test_faf_string_mem.o $(OBJ_DIR)/faf_string_mem.o $(OBJ_DIR)/faf_string_strlen.o -o $@ -g

test: $(BIN_DIR)/test_faf_string $(BIN_DIR)/test_faf_string_mem

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean

