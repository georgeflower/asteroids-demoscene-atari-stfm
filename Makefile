PROJECT := ASTROIDS
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

CROSS ?= m68k-atari-mint-
CC := $(CROSS)gcc
HOST_CC ?= cc
CFLAGS := -std=c99 -m68000 -O2 -Wall -Wextra -Werror -fomit-frame-pointer -I$(INC_DIR)
ASFLAGS := -m68000
LDFLAGS := -s
TARGET := $(BUILD_DIR)/$(PROJECT).PRG
DISK_IMAGE := $(BUILD_DIR)/asteroids-stfm.st
TEST_TARGET := $(BUILD_DIR)/game_tests

ATARI_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c
ATARI_ASMS := $(SRC_DIR)/st_video.S $(SRC_DIR)/st_ikbd.S
ATARI_OBJS := $(ATARI_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(ATARI_ASMS:$(SRC_DIR)/%.S=$(BUILD_DIR)/%.o)
TEST_SRCS := $(TEST_DIR)/game_tests.c $(SRC_DIR)/game.c $(SRC_DIR)/sound.c

.PHONY: all atari-st disk-image test test-atari test-gfx-atari test-keys-atari test-sound-atari screens-atari bench-atari host-sanity clean

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

# The same tests as a TOS program (prints results, waits for a key), for running in Hatari.
test-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_SRCS) $(LDFLAGS) -o $(BUILD_DIR)/GAMETEST.PRG

# Pixel-exact checks of the assembly drawing routines (run in Hatari).
test-gfx-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_gfx_test.c $(SRC_DIR)/st_video.S $(SRC_DIR)/st_text.c $(LDFLAGS) -o $(BUILD_DIR)/GFXTEST.PRG

# Keyboard diagnostic: logs which keys reach the game (run in Hatari, read C:\KEYTEST.LOG).
test-keys-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_key_test.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/KEYTEST.PRG

# Sound path on the emulated hardware: plays effects and reads the YM registers back (run in Hatari, read C:\SNDTEST.LOG).
test-sound-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_sound_test.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/SNDTEST.PRG

# Scripted walk through the banner, pause, game over, initials and title screens (run in Hatari, take screenshots).
screens-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_screens.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/SCREENS.PRG

# Frame-cost benchmark (run in Hatari): game step + clear + render for a few wave sizes.
bench-atari: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_bench.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/GAMEBENCH.PRG

host-sanity: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(BUILD_DIR)/main.host.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/platform_atari_st.c -o $(BUILD_DIR)/platform_atari_st.host.o

clean:
	rm -rf $(BUILD_DIR)
