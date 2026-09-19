PROJECT := ASTROIDS
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

CROSS ?= m68k-atari-mint-
CC := $(CROSS)gcc
HOST_CC ?= cc
CFLAGS := -std=c99 -m68000 -Os -Wall -Wextra -Werror -fomit-frame-pointer -I$(INC_DIR)
ASFLAGS := -m68000
LDFLAGS := -s
TARGET := $(BUILD_DIR)/$(PROJECT).PRG
DISK_IMAGE := $(BUILD_DIR)/asteroids-stfm.st
TEST_TARGET := $(BUILD_DIR)/game_tests

ATARI_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c
ATARI_ASMS := $(SRC_DIR)/st_video.S
ATARI_OBJS := $(ATARI_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(ATARI_ASMS:$(SRC_DIR)/%.S=$(BUILD_DIR)/%.o)
TEST_SRCS := $(TEST_DIR)/game_tests.c $(SRC_DIR)/game.c

.PHONY: all atari-st disk-image test host-sanity clean

all: atari-st disk-image

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET): $(ATARI_OBJS)
	$(CC) $(CFLAGS) $(ATARI_OBJS) $(LDFLAGS) -o $@

atari-st: $(TARGET)

$(DISK_IMAGE): $(TARGET) | $(BUILD_DIR)
	rm -f $@
	truncate -s 737280 $@
	mformat -i $@ -f 720 -v ASTEROIDS ::
	mcopy -i $@ $(TARGET) ::ASTROIDS.PRG

disk-image: $(DISK_IMAGE)

test: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) $(TEST_SRCS) -o $(TEST_TARGET)
	$(TEST_TARGET)

host-sanity: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(BUILD_DIR)/main.host.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/platform_atari_st.c -o $(BUILD_DIR)/platform_atari_st.host.o

clean:
	rm -rf $(BUILD_DIR)
