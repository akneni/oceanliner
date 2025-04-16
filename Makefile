CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

OBJ_DIR = obj
SRC = src/raft_core.c
TEST = tests/test_raft_core.c
OBJ = $(OBJ_DIR)/raft_core.o $(OBJ_DIR)/test_raft_core.o

$(shell mkdir -p $(OBJ_DIR))

all: test

$(OBJ_DIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o test_raft_core

clean:
	rm -rf $(OBJ_DIR) test_raft_core

.PHONY: all clean