# Define the compiler and options
CC = gcc-14
CFLAGS = -O3 -I$(shell brew --prefix simde)/include/ -flax-vector-conversions

# Targets and their source files
TARGETS = generate_random_strings str_split_test str_len_test
SRC_DIR = .
OBJ_DIR = obj
BIN_DIR = bin

# Create necessary directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(wildcard $(SRC_DIR)/*.c))

# Default target
all: $(BIN_DIR) $(OBJ_DIR) $(TARGETS)

# Compile each target
$(TARGETS): %: $(OBJ_DIR)/%.o
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$@ $<

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean

