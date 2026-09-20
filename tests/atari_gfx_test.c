/*
 * Pixel-exact checks of the assembly drawing routines, run on the ST (in Hatari): make test-gfx-atari.
 *  - st_draw_poly_plane must match st_draw_line_plane edge by edge
 *  - st_draw_line_plane must match the original four-plane st_draw_line_low
 *  - st_clear_rect must match a plain C reference
 *  - st_text_draw must match a pixel-by-pixel reference, at both text scales
 */
#include "font_data.h"
#include "st_text.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_BYTES 32000
#define WIDTH 320
#define HEIGHT 200

extern void st_clear_buffer(unsigned char *buffer);
extern void st_draw_line_low(unsigned char *buffer, long x0, long y0, long x1, long y1, long color);
extern void st_draw_line_plane(unsigned char *buffer, long x0, long y0, long x1, long y1, long plane_offset);
extern void st_draw_poly_plane(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_pair(unsigned char *buffer, long x, long y, long plane_offset);
extern void st_clear_rects(unsigned char *buffer, const short *rects, long count);
extern void st_clear_rect(unsigned char *buffer, long group0, long group1, long y0, long y1);

static unsigned char buffer_a[SCREEN_BYTES + 2];
static unsigned char buffer_b[SCREEN_BYTES + 2];
static unsigned long rng = 12345;
static int failures;
static int checks;

static unsigned rand_below(unsigned limit) {
    rng = rng * 1103515245ul + 12345ul;
    return (unsigned) (((rng >> 16) & 0xffffu) * limit >> 16);
}

static void report(const char *what, int detail) {
    ++failures;
    if (failures <= 6) {
        printf("FAIL %s %d\n", what, detail);
    }
}

static void test_polygons(void) {
    int plane;
    int trial;

    for (plane = 0; plane < 4; ++plane) {
        for (trial = 0; trial < 700; ++trial) {
            short points[24];
            const int count = 1 + (int) rand_below(11);
            int index;
            int mode = trial % 5;

            for (index = 0; index < count; ++index) {
                int x = (int) rand_below(WIDTH);
                int y = (int) rand_below(HEIGHT);

                if (mode == 1) {                 /* hug the screen edges */
                    x = (rand_below(2) != 0) ? 0 : WIDTH - 1;
                    y = (rand_below(2) != 0) ? 0 : HEIGHT - 1;
                } else if (mode == 2) {          /* short lines close together */
                    x = 100 + (int) rand_below(12);
                    y = 60 + (int) rand_below(12);
                } else if (mode == 3 && index > 0) {   /* axis-aligned steps */
                    x = (index & 1) ? points[(index - 1) * 2] : x;
                    y = (index & 1) ? y : points[(index - 1) * 2 + 1];
                }
                points[index * 2] = (short) x;
                points[index * 2 + 1] = (short) y;
            }

            memset(buffer_a, 0, sizeof(buffer_a));
            memset(buffer_b, 0, sizeof(buffer_b));
            st_draw_poly_plane(buffer_a, points, count, plane * 2);
            for (index = 0; index < count; ++index) {
                const int next = (index + 1 == count) ? 0 : index + 1;
                st_draw_line_plane(buffer_b, points[index * 2], points[index * 2 + 1], points[next * 2],
                                   points[next * 2 + 1], plane * 2);
            }
            ++checks;
            if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
                report("polygon vs lines, plane", plane);
            }
        }
    }
}

/* A list of rectangles must clear exactly what st_clear_rect does for each of them. */
static void test_clear_rects(void) {
    int trial;

    for (trial = 0; trial < 300; ++trial) {
        short rects[12][4];
        const int count = 1 + (int) rand_below(12);
        int index;
        int fill;

        for (fill = 0; fill < SCREEN_BYTES; ++fill) {
            buffer_a[fill] = (unsigned char) (fill * 5 + trial + 1);
            buffer_b[fill] = buffer_a[fill];
        }
        for (index = 0; index < count; ++index) {
            const int width = 1 + (int) rand_below(trial % 3 == 0 ? 120 : 40);
            const int height = 1 + (int) rand_below(40);
            const int x0 = (int) rand_below((unsigned) (WIDTH - width));
            const int y0 = (int) rand_below((unsigned) (HEIGHT - height));

            rects[index][0] = (short) x0;
            rects[index][1] = (short) y0;
            rects[index][2] = (short) (x0 + width - 1);
            rects[index][3] = (short) (y0 + height - 1);
            st_clear_rect(buffer_b, rects[index][0] >> 4, rects[index][2] >> 4, rects[index][1], rects[index][3]);
        }
        st_clear_rects(buffer_a, &rects[0][0], count);
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("clear_rects vs clear_rect", trial);
        }
    }
}

/* A bullet (two pixels side by side) must match the line drawer, including across a 16-pixel group boundary. */
static void test_pair(void) {
    int trial;

    for (trial = 0; trial < 1500; ++trial) {
        const int x = (trial < 400) ? (trial % 20) * 16 + 15 - (trial % 2) : (int) rand_below(WIDTH - 1);
        const int y = (int) rand_below(HEIGHT);
        const int plane = trial & 3;

        if (x + 1 >= WIDTH) {
            continue;
        }
        memset(buffer_a, 0, sizeof(buffer_a));
        memset(buffer_b, 0, sizeof(buffer_b));
        st_draw_pair(buffer_a, x, y, plane * 2);
        st_draw_line_plane(buffer_b, x, y, x + 1, y, plane * 2);
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("pair vs line, x", x);
        }
    }
}

/* An open polyline must match its lines drawn one by one, including 1 and 2 points. */
static void test_polyline(void) {
    int trial;

    for (trial = 0; trial < 1500; ++trial) {
        short points[24];
        const int count = 1 + (int) rand_below(12);
        const int plane = trial & 3;
        const int spread = (trial % 3 == 0) ? 12 : 160;
        int index;

        for (index = 0; index < count; ++index) {
            points[index * 2] = (short) (100 + (int) rand_below((unsigned) spread * 2) - spread + 60);
            points[index * 2 + 1] = (short) (50 + (int) rand_below((unsigned) (spread < 100 ? spread * 2 : 150)));
        }
        memset(buffer_a, 0, sizeof(buffer_a));
        memset(buffer_b, 0, sizeof(buffer_b));
        st_draw_polyline(buffer_a, points, count, plane * 2);
        if (count == 1) {
            st_draw_line_plane(buffer_b, points[0], points[1], points[0], points[1], plane * 2);
        }
        for (index = 0; index + 1 < count; ++index) {
            st_draw_line_plane(buffer_b, points[index * 2], points[index * 2 + 1], points[index * 2 + 2],
                               points[index * 2 + 3], plane * 2);
        }
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("polyline vs lines", trial);
        }
    }
}

static void test_plane_matches_four_plane_drawer(void) {
    int plane;
    int trial;

    for (plane = 0; plane < 4; ++plane) {
        for (trial = 0; trial < 500; ++trial) {
            const long x0 = (long) rand_below(WIDTH);
            const long y0 = (long) rand_below(HEIGHT);
            long x1 = (long) rand_below(WIDTH);
            long y1 = (long) rand_below(HEIGHT);

            if (trial % 4 == 1) {
                y1 = y0;
            } else if (trial % 4 == 2) {
                x1 = x0;
            }
            memset(buffer_a, 0, sizeof(buffer_a));
            memset(buffer_b, 0, sizeof(buffer_b));
            st_draw_line_plane(buffer_a, x0, y0, x1, y1, plane * 2);
            st_draw_line_low(buffer_b, x0, y0, x1, y1, 1L << plane);
            ++checks;
            if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
                report("plane line vs four-plane line, plane", plane);
            }
        }
    }
}

static void test_clear_rect(void) {
    int trial;

    for (trial = 0; trial < 400; ++trial) {
        long g0 = (long) rand_below(20);
        long g1 = (long) rand_below(20);
        long y0 = (long) rand_below(HEIGHT);
        long y1 = (long) rand_below(HEIGHT);
        long tmp;
        long row;
        long group;
        long byte;

        if (g0 > g1) {
            tmp = g0;
            g0 = g1;
            g1 = tmp;
        }
        if (y0 > y1) {
            tmp = y0;
            y0 = y1;
            y1 = tmp;
        }
        memset(buffer_a, 0xff, sizeof(buffer_a));
        memset(buffer_b, 0xff, sizeof(buffer_b));
        st_clear_rect(buffer_a, g0, g1, y0, y1);
        for (row = y0; row <= y1; ++row) {
            for (group = g0; group <= g1; ++group) {
                for (byte = 0; byte < 8; ++byte) {
                    buffer_b[row * 160 + group * 8 + byte] = 0;
                }
            }
        }
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES + 2) != 0) {
            report("clear rect", (int) trial);
        }
    }
}


/* ---- text ---- */

static int reference_glyph_row(char ch, int row) {
    int code = (unsigned char) ch;

    if (code >= 'a' && code <= 'z') {
        code -= 32;
    }
    if (code == 127) {
        return font_data[95][row];
    }
    if (code < 32 || code > 126) {
        code = 32;
    }
    return font_data[code - 32][row];
}

/* Write one pixel of palette index `color` into a planar buffer, plane by plane. */
static void reference_pixel(unsigned char *buffer, int x, int y, int color) {
    int plane;

    for (plane = 0; plane < 4; ++plane) {
        unsigned char *target = buffer + y * 160 + (x >> 4) * 8 + plane * 2 + ((x >> 3) & 1);
        const unsigned char mask = (unsigned char) (0x80 >> (x & 7));

        if ((color >> plane) & 1) {
            *target = (unsigned char) (*target | mask);
        } else {
            *target = (unsigned char) (*target & ~mask);
        }
    }
}

static void reference_text(unsigned char *buffer, int x, int y, const char *text, int fg, int bg, int scale) {
    const int cell = (scale == 2) ? 16 : 8;

    x -= x % cell;
    for (; *text != 0; ++text, x += cell) {
        int row;
        int column;

        for (row = 0; row < 8; ++row) {
            const int bits = reference_glyph_row(*text, row);
            for (column = 0; column < 8; ++column) {
                const int color = (bits & (0x80 >> column)) ? fg : bg;
                int dx;
                int dy;

                for (dy = 0; dy < scale; ++dy) {
                    for (dx = 0; dx < scale; ++dx) {
                        reference_pixel(buffer, x + column * scale + dx, y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static void test_plot_point(void) {
    int trial;

    for (trial = 0; trial < 400; ++trial) {
        const int x = (int) rand_below(WIDTH);
        const int y = (int) rand_below(HEIGHT);
        const int color = (int) rand_below(16);
        int index;

        for (index = 0; index < SCREEN_BYTES; ++index) {
            buffer_a[index] = (unsigned char) (index * 5 + trial);
            buffer_b[index] = buffer_a[index];
        }
        st_plot_point(buffer_a, x, y, color);
        reference_pixel(buffer_b, x, y, color);
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("plot point", trial);
        }
    }
    /* points outside the screen are ignored */
    for (trial = 0; trial < 8; ++trial) {
        static const int outside[8][2] = {{-1, 5}, {5, -1}, {320, 5}, {5, 200}, {-100, -100}, {400, 50}, {50, 300}, {320, 200}};

        memset(buffer_a, 0x5a, SCREEN_BYTES);
        memset(buffer_b, 0x5a, SCREEN_BYTES);
        st_plot_point(buffer_a, outside[trial][0], outside[trial][1], 15);
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("plot point outside", trial);
        }
    }
}

static void test_text(void) {
    static const char *const samples[] = {
        "SCORE 001234", "HYPER 50%  ", "A/D OR ARROWS TURN  W OR UP THRUST", "lower case", "!?:.-,<=>()' _^*+/",
        "0123456789", " X9"
    };
    int trial;

    for (trial = 0; trial < 300; ++trial) {
        const char *text = samples[trial % 7];
        const int scale = 1 + (trial % 3 == 0);
        const int cell = (scale == 2) ? 16 : 8;
        const int length = (int) strlen(text);
        const int width = length * cell;
        const int max_x = WIDTH - width;
        int x;
        int y;
        int fg = (int) rand_below(16);
        int bg = (int) rand_below(16);

        if (max_x < 0) {
            continue;
        }
        x = (int) rand_below((unsigned) max_x + 1) + (int) (trial % 5);   /* not always aligned: rounds down */
        if (x + width > WIDTH) {
            x = max_x;
        }
        y = (int) rand_below((unsigned) (HEIGHT - 8 * scale + 1));

        /* a busy background: whatever the text does not touch must stay as it was */
        for (x = 0; x < SCREEN_BYTES; ++x) {
            buffer_a[x] = (unsigned char) (x * 7 + trial);
            buffer_b[x] = buffer_a[x];
        }
        x = (int) rand_below((unsigned) max_x + 1);
        st_text_draw(buffer_a, x, y, text, fg, bg, scale);
        reference_text(buffer_b, x, y, text, fg, bg, scale);
        ++checks;
        if (memcmp(buffer_a, buffer_b, SCREEN_BYTES) != 0) {
            report("text, sample", trial % 7);
        }
    }
}

int main(void) {
    test_polygons();
    test_polyline();
    test_pair();
    test_clear_rects();
    test_plane_matches_four_plane_drawer();
    test_clear_rect();
    test_text();
    test_plot_point();
    printf("%d checks, %d failures\n", checks, failures);
    printf("press a key\n");
    getchar();
    return failures != 0;
}
