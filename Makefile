PROJECT := asteroids-stfm
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

CROSS ?= m68k-atari-mint-
CC := $(CROSS)gcc
HOST_CC ?= cc
CFLAGS := -std=c99 -Os -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I$(INC_DIR)
LDFLAGS := -Wl,--gc-sections
TARGET := $(BUILD_DIR)/$(PROJECT).tos
TEST_TARGET := $(BUILD_DIR)/game_tests

ATARI_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c
ATARI_OBJS := $(ATARI_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_SRCS := $(TEST_DIR)/game_tests.c $(SRC_DIR)/game.c

.PHONY: all atari-st test host-sanity clean

all: atari-st

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET -c $< -o $@

$(TARGET): $(ATARI_OBJS)
	$(CC) $(CFLAGS) $(ATARI_OBJS) $(LDFLAGS) -o $@

atari-st: $(TARGET)

test: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) $(TEST_SRCS) -o $(TEST_TARGET)
	$(TEST_TARGET)

host-sanity: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(BUILD_DIR)/main.host.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/platform_atari_st.c -o $(BUILD_DIR)/platform_atari_st.host.o

clean:
	rm -rf $(BUILD_DIR)
