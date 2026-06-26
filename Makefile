#José Miguel Luís Antunes, 2023211288
#André Jorge Balula Leão, 2023210870
 
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lrt -lssl -lcrypto

# Directories
SRC_DIR = /home/user/Desktop/Projecto/src
OBJ_DIR = /home/user/Desktop/Projecto/obj
BIN_DIR = /home/user/Desktop/Projecto/bin

# Source files
SRCS = $(SRC_DIR)/Controller.c $(SRC_DIR)/TxGen.c $(SRC_DIR)/handler.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Executables
TARGETS = $(BIN_DIR)/controller $(BIN_DIR)/TxGen

# Default target
all: $(TARGETS)

# Rule to build each executable
$(BIN_DIR)/controller: $(OBJ_DIR)/Controller.o $(OBJ_DIR)/handler.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/TxGen: $(OBJ_DIR)/TxGen.o $(OBJ_DIR)/handler.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Rule to compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories if missing
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
