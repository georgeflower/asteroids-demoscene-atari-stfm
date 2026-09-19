/*
 * Anti-aliasing experiment: draws the same ship and rocks with the normal one-pixel lines (left) and with
 * smooth lines (right, a 2-step ramp made from two bitplanes), then times both. Results go to C:\AATEST.LOG.
 * Run in Hatari and take a screenshot: make aa-atari.
 */
#include "platform.h"
#include "trig_table.h"

#include <stdio.h>
#include <string.h>

#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define ST_HW_PALETTE ((volatile uint16_t *) 0xff8240UL)

static int sin_q14(unsigned angle) {   /* angle: 256 = one turn */
    return game_sin_table[angle & 255];
}

static int cos_q14(unsigned angle) {
    return game_sin_table[(angle + 64) & 255];
}

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'A', 'A', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static int build_ship(int16_t *out, int cx, int cy, unsigned angle) {
    static const int8_t shape[4][2] = {{8, 0}, {-8, -4}, {-4, 0}, {-8, 4}};
    int index;

    for (index = 0; index < 4; ++index) {
        const int x = shape[index][0] * 2;
        const int y = shape[index][1] * 2;

        out[index * 2] = (int16_t) (cx + ((x * cos_q14(angle) - y * sin_q14(angle)) >> 14));
        out[index * 2 + 1] = (int16_t) (cy + ((x * sin_q14(angle) + y * cos_q14(angle)) >> 14));
    }
    return 4;
}

static int build_rock(int16_t *out, int cx, int cy, int radius, unsigned angle, unsigned seed) {
    int index;

    for (index = 0; index < 10; ++index) {
        const unsigned a = angle + (unsigned) index * 25 + (seed & 3);
        const int r = radius * (70 + (int) (((seed >> index) * 7 + index * 13) % 31)) / 100;

        out[index * 2] = (int16_t) (cx + ((r * cos_q14(a)) >> 14));
        out[index * 2 + 1] = (int16_t) (cy + ((r * sin_q14(a)) >> 14));
    }
    return 10;
}

static void draw(int smooth, int x_offset, int frame) {
    int16_t points[24];
    int count;
    int index;

    for (index = 0; index < 4; ++index) {
        count = build_ship(points, x_offset + 30 + index * 34, 40, (unsigned) (index * 21 + frame));
        if (smooth) {
            platform_draw_aa_polygon(points, count, 0, 1);
        } else {
            platform_draw_polygon(NULL, points, count, 4);
        }
    }
    for (index = 0; index < 4; ++index) {
        count = build_rock(points, x_offset + 30 + index * 34, 90, 22 - index * 4, (unsigned) (index * 30 + frame), (unsigned) index * 5 + 3);
        if (smooth) {
            platform_draw_aa_polygon(points, count, 0, 1);
        } else {
            platform_draw_polygon(NULL, points, count, 4);
        }
    }
    for (index = 0; index < 4; ++index) {
        count = build_rock(points, x_offset + 30 + index * 34, 140, 12 - index, (unsigned) (index * 47 + frame), (unsigned) index * 9 + 1);
        if (smooth) {
            platform_draw_aa_polygon(points, count, 0, 1);
        } else {
            platform_draw_polygon(NULL, points, count, 4);
        }
    }
}

int main(void) {
    uint32_t start;
    uint32_t plain_ticks;
    uint32_t smooth_ticks;
    int frame;
    int page;
    char text[80];

    if (!platform_init()) {
        return 1;
    }
    /* plain lines on bitplane 2 (index 4), smooth ones on bitplanes 0 and 1: 1 = half lit, 3 = fully lit */
    ST_HW_PALETTE[0] = 0x000;
    ST_HW_PALETTE[1] = 0x333;
    ST_HW_PALETTE[2] = 0x777;
    ST_HW_PALETTE[3] = 0x777;
    ST_HW_PALETTE[4] = 0x777;
    ST_HW_PALETTE[5] = 0x124;

    for (page = 0; page < 2; ++page) {
        platform_begin_frame();
        platform_clear_field(NULL);
        draw(0, 16, 0);
        draw(1, 160, 0);
        platform_end_frame();
    }

    start = ST_HZ200;
    for (frame = 0; frame < 100; ++frame) {
        draw(0, 16, frame);
    }
    plain_ticks = ST_HZ200 - start;
    start = ST_HZ200;
    for (frame = 0; frame < 100; ++frame) {
        draw(1, 160, frame);
    }
    smooth_ticks = ST_HZ200 - start;

    sprintf(text, "12 shapes x 100: plain %lu ticks (200 Hz), smooth %lu ticks", (unsigned long) plain_ticks,
            (unsigned long) smooth_ticks);
    log_line(text);

    /* hold the picture for a screenshot */
    start = ST_HZ200;
    while (ST_HZ200 - start < 800) {
    }
    platform_shutdown();
    return 0;
}
