/*
 * Checks the sound path on the emulated ST hardware: plays effects through the real register writer
 * (interrupts masked around select+write) and reads the YM2149 registers back from the chip.
 * Results go to C:\SNDTEST.LOG. Run in Hatari: make test-sound-atari.
 */
#include "platform.h"
#include "sound.h"

#include <stdio.h>
#include <string.h>

#define PSG_SELECT (*(volatile uint8_t *) 0xff8800UL)
#define PSG_READ (*(volatile uint8_t *) 0xff8800UL)
#define ST_FRCLOCK (*(volatile uint32_t *) 0x466UL)

static int failures;
static int checks;

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'S', 'N', 'D', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");
    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static int read_register(int reg) {
    PSG_SELECT = (uint8_t) reg;
    return PSG_READ;
}

static void expect(const char *what, int reg, int value, int mask) {
    const int actual = read_register(reg);
    char text[100];

    ++checks;
    if ((actual & mask) != (value & mask)) {
        ++failures;
        sprintf(text, "FAIL %s: reg %d = %d, wanted %d (mask %d)", what, reg, actual, value, mask);
        log_line(text);
    }
}

static void frames(int count) {
    while (count-- > 0) {
        const uint32_t before = ST_FRCLOCK;

        sound_tick();
        while (ST_FRCLOCK == before) {
        }
    }
}

int main(void) {
    char text[80];

    if (!platform_init()) {
        return 1;
    }
    log_line("start");
    sound_init(platform_sound_write);

    expect("silent volume A", 8, 0, 15);
    expect("silent volume B", 9, 0, 15);
    expect("silent volume C", 10, 0, 15);
    expect("mixer off", 7, 0xff, 0xff);

    sound_play(SFX_SHOOT);
    frames(1);
    expect("shot tone low", 0, 150, 0xff);
    expect("shot tone high", 1, 0, 0x0f);
    expect("shot volume", 8, 9, 15);
    expect("shot mixer: tone A on, ports out", 7, 0xfe, 0xff);
    frames(5);
    expect("shot pitch falls", 0, 150 + 12 * 5, 0xff);
    expect("shot volume decays", 8, 9 - 5, 15);
    frames(20);
    expect("shot over", 8, 0, 15);

    sound_play(SFX_EXPLODE_LARGE);
    frames(1);
    expect("explosion noise period", 6, 28, 31);
    expect("explosion volume", 9, 15, 15);
    expect("explosion mixer: noise B on", 7, 0xef, 0xff);
    frames(40);
    expect("explosion over", 9, 0, 15);

    sound_set_thrust(1);
    frames(3);
    expect("thrust volume", 10, 6, 15);
    sound_set_thrust(0);
    frames(1);
    expect("thrust off", 10, 0, 15);

    sound_play(SFX_HYPERSPACE);
    frames(1);
    expect("hyperspace tone low byte", 0, 400 & 0xff, 0xff);
    expect("hyperspace tone high nibble", 1, 400 >> 8, 0x0f);

    sound_silence();
    expect("silenced A", 8, 0, 15);
    expect("silenced C", 10, 0, 15);
    expect("silenced mixer", 7, 0xff, 0xff);

    sprintf(text, "%d checks, %d failures", checks, failures);
    log_line(text);
    log_line("done");
    platform_shutdown();
    return failures != 0;
}
