/*
 * Checks of the platform's polygon drawing on the real screen memory (run in Hatari: make test-poly-atari):
 *  - polygons partly outside the playing field (fast runs + clipped edges) look exactly like the same
 *    polygon drawn edge by edge with platform_draw_line
 *  - rocks drawn from centre + byte offsets look exactly like the point-list polygon
 *  - the muls/divs clipping arithmetic equals C's (a * b) / c
 * Results go to C:\POLYTEST.LOG.
 */
#include "platform.h"

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_BYTES 32000

static unsigned long rng = 4242;
static int failures;
static int checks;
static unsigned char shot_a[SCREEN_BYTES];
static unsigned char shot_b[SCREEN_BYTES];

static unsigned rand_below(unsigned limit) {
    rng = rng * 1103515245ul + 12345ul;
    return (unsigned) (((rng >> 16) & 0xffffu) * limit >> 16);
}

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'P', 'O', 'L', 'Y', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static void fail(const char *what, int detail) {
    char text[80];

    ++failures;
    if (failures <= 8) {
        sprintf(text, "FAIL %s %d", what, detail);
        log_line(text);
    }
}

/* Draw with the given routine on a cleared field, flip, and copy the screen that is now showing. */
static void capture(unsigned char *out, void (*draw)(const int16_t *points, int count, uint8_t color, const int8_t *ox,
                                                      const int8_t *oy, int cx, int cy),
                    const int16_t *points, int count, uint8_t color, const int8_t *ox, const int8_t *oy, int cx,
                    int cy) {
    platform_clear_field(NULL);
    draw(points, count, color, ox, oy, cx, cy);
    platform_end_frame();
    memcpy(out, Physbase(), SCREEN_BYTES);
}

static void draw_polygon(const int16_t *points, int count, uint8_t color, const int8_t *ox, const int8_t *oy, int cx,
                         int cy) {
    (void) ox;
    (void) oy;
    (void) cx;
    (void) cy;
    platform_draw_polygon(NULL, points, count, color);
}

static void draw_edges(const int16_t *points, int count, uint8_t color, const int8_t *ox, const int8_t *oy, int cx,
                       int cy) {
    int index;

    (void) ox;
    (void) oy;
    (void) cx;
    (void) cy;
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;

        platform_draw_line(NULL, points[index * 2], points[index * 2 + 1], points[next * 2], points[next * 2 + 1], color);
    }
}

static void draw_offsets(const int16_t *points, int count, uint8_t color, const int8_t *ox, const int8_t *oy, int cx,
                         int cy) {
    static uint16_t cache[GAME_DRAW_CACHE_WORDS];
    uint8_t valid = 0;

    (void) points;
    platform_draw_polygon_offsets(NULL, cx, cy, ox, oy, count, color, cache, &valid);
}

static long muldiv16(long a, long b, long c) {
    long value = (long) (short) a * (short) b;

    __asm__ ("divs.w %1,%0\n\text.l %0" : "+d" (value) : "dm" ((short) c) : "cc");
    return value;
}

int main(void) {
    static const uint8_t colors[4] = {1, 2, 4, 8};
    int trial;
    char text[80];

    if (!platform_init()) {
        return 1;
    }

    for (trial = 0; trial < 1200; ++trial) {
        int16_t points[24];
        const int count = 3 + (int) rand_below(10);
        const int cx = (int) rand_below(320);
        const int cy = (int) rand_below(200);
        const int spread = 4 + (int) rand_below(24);
        int index;

        /* polygons around the border of the field, some entirely outside, some entirely inside */
        for (index = 0; index < count; ++index) {
            points[index * 2] = (int16_t) (cx + (int) rand_below((unsigned) spread * 2 + 1) - spread);
            points[index * 2 + 1] = (int16_t) (cy + (int) rand_below((unsigned) spread * 2 + 1) - spread);
            if (points[index * 2] < 0) {
                points[index * 2] = 0;
            }
            if (points[index * 2 + 1] < 0) {
                points[index * 2 + 1] = 0;
            }
            if (points[index * 2] > 319) {
                points[index * 2] = 319;
            }
            if (points[index * 2 + 1] > 199) {
                points[index * 2 + 1] = 199;
            }
        }
        capture(shot_a, draw_polygon, points, count, colors[trial & 3], NULL, NULL, 0, 0);
        capture(shot_b, draw_edges, points, count, colors[trial & 3], NULL, NULL, 0, 0);
        ++checks;
        if (memcmp(shot_a, shot_b, SCREEN_BYTES) != 0) {
            fail("partial polygon", trial);
        }
    }

    for (trial = 0; trial < 400; ++trial) {
        int8_t ox[12];
        int8_t oy[12];
        int16_t points[24];
        const int count = 6 + (int) rand_below(6);
        const int cx = 40 + (int) rand_below(240);
        const int cy = 40 + (int) rand_below(120);
        int index;

        for (index = 0; index < count; ++index) {
            ox[index] = (int8_t) ((int) rand_below(37) - 18);
            oy[index] = (int8_t) ((int) rand_below(37) - 18);
            points[index * 2] = (int16_t) (cx + ox[index]);
            points[index * 2 + 1] = (int16_t) (cy + oy[index]);
        }
        capture(shot_a, draw_offsets, points, count, colors[trial & 3], ox, oy, cx, cy);
        capture(shot_b, draw_polygon, points, count, colors[trial & 3], NULL, NULL, 0, 0);
        ++checks;
        if (memcmp(shot_a, shot_b, SCREEN_BYTES) != 0) {
            fail("offsets polygon", trial);
        }
    }

    for (trial = 0; trial < 20000; ++trial) {
        const long c = (long) rand_below(400) + 1;
        const long a = (long) rand_below(700) - 350;
        const long b = ((long) rand_below((unsigned) (c * 2 + 1)) - c);
        const long cc = (trial & 1) ? c : -c;

        ++checks;
        if (muldiv16(a, b, cc) != (a * b) / cc) {
            fail("muldiv16", trial);
        }
    }

    sprintf(text, "%d checks, %d failures", checks, failures);
    log_line(text);
    platform_shutdown();
    return failures != 0;
}
