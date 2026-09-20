#include "st_text.h"

#include "font_data.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define ROW_BYTES 160

static const unsigned char *glyph_for(char ch) {
    int code = (unsigned char) ch;

    if (code >= 'a' && code <= 'z') {
        code -= 32;
    }
    if (code == 127) {
        return font_data[95];
    }
    if (code < 32 || code > 126) {
        code = 32;
    }
    return font_data[code - 32];
}

/* Double every bit of an 8-bit row into 16 bits. */
static unsigned short double_bits(unsigned char row) {
    unsigned short wide = 0;
    int bit;

    for (bit = 0; bit < 8; ++bit) {
        if (row & (0x80 >> bit)) {
            wide |= (unsigned short) (3u << (14 - 2 * bit));
        }
    }
    return wide;
}

static void draw_cell_1x(unsigned char *buffer, int x, int y, const unsigned char *glyph, int fg, int bg) {
    const int cell = x >> 3;
    const int group_offset = (cell >> 1) * 8 + (cell & 1);
    int row;

    for (row = 0; row < 8; ++row) {
        unsigned char *target = buffer + (y + row) * ROW_BYTES + group_offset;
        const unsigned char bits = glyph[row];
        int plane;

        for (plane = 0; plane < 4; ++plane) {
            unsigned char value = 0;
            if ((fg >> plane) & 1) {
                value = (unsigned char) (value | bits);
            }
            if ((bg >> plane) & 1) {
                value = (unsigned char) (value | (unsigned char) ~bits);
            }
            target[plane * 2] = value;
        }
    }
}

static void draw_cell_2x(unsigned char *buffer, int x, int y, const unsigned char *glyph, int fg, int bg) {
    const int group_offset = (x >> 4) * 8;
    int row;

    for (row = 0; row < 8; ++row) {
        const unsigned short wide = double_bits(glyph[row]);
        int copy;

        for (copy = 0; copy < 2; ++copy) {
            unsigned short *target = (unsigned short *) (buffer + (y + row * 2 + copy) * ROW_BYTES + group_offset);
            int plane;

            for (plane = 0; plane < 4; ++plane) {
                unsigned short value = 0;
                if ((fg >> plane) & 1) {
                    value = (unsigned short) (value | wide);
                }
                if ((bg >> plane) & 1) {
                    value = (unsigned short) (value | (unsigned short) ~wide);
                }
                target[plane] = value;
            }
        }
    }
}

/* Widen the four bits of a nibble to sixteen, each four times. */
static unsigned short quadruple_bits(unsigned char nibble) {
    unsigned short wide = 0;
    int bit;

    for (bit = 0; bit < 4; ++bit) {
        if (nibble & (8 >> bit)) {
            wide |= (unsigned short) (15u << (12 - 4 * bit));
        }
    }
    return wide;
}

/* Every pixel of the glyph becomes a 4x4 block: a 32x32 cell that starts on a 16-pixel boundary. */
static void draw_cell_4x(unsigned char *buffer, int x, int y, const unsigned char *glyph, int fg, int bg) {
    const int group_offset = (x >> 4) * 8;
    int row;

    for (row = 0; row < 8; ++row) {
        const unsigned short halves[2] = {quadruple_bits((unsigned char) (glyph[row] >> 4)),
                                          quadruple_bits((unsigned char) (glyph[row] & 15))};
        int copy;

        for (copy = 0; copy < 4; ++copy) {
            int half;

            for (half = 0; half < 2; ++half) {
                unsigned short *target =
                    (unsigned short *) (buffer + (y + row * 4 + copy) * ROW_BYTES + group_offset + half * 8);
                int plane;

                for (plane = 0; plane < 4; ++plane) {
                    unsigned short value = 0;
                    if ((fg >> plane) & 1) {
                        value = (unsigned short) (value | halves[half]);
                    }
                    if ((bg >> plane) & 1) {
                        value = (unsigned short) (value | (unsigned short) ~halves[half]);
                    }
                    target[plane] = value;
                }
            }
        }
    }
}

/* y * 160 without a 32-bit multiply (the 68000 has none): a table, filled on first use. */
static short row_offset[SCREEN_HEIGHT];
static int row_offset_ready;

void st_plot_point(unsigned char *buffer, int x, int y, int color) {
    unsigned char *row;
    unsigned char mask;
    unsigned char keep;

    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    if (!row_offset_ready) {
        int line;

        for (line = 0; line < SCREEN_HEIGHT; ++line) {
            row_offset[line] = (short) (line * ROW_BYTES);
        }
        row_offset_ready = 1;
    }
    row = buffer + row_offset[y] + ((x >> 4) << 3) + ((x >> 3) & 1);
    mask = (unsigned char) (0x80 >> (x & 7));
    keep = (unsigned char) ~mask;
    row[0] = (unsigned char) ((row[0] & keep) | ((color & 1) ? mask : 0));
    row[2] = (unsigned char) ((row[2] & keep) | ((color & 2) ? mask : 0));
    row[4] = (unsigned char) ((row[4] & keep) | ((color & 4) ? mask : 0));
    row[6] = (unsigned char) ((row[6] & keep) | ((color & 8) ? mask : 0));
}

void st_text_draw(unsigned char *buffer, int x, int y, const char *text, int fg, int bg, int scale) {
    const int cell_width = 8 * scale;
    const int cell_height = 8 * scale;

    x -= x % (scale >= 2 ? 16 : 8);
    if (y < 0 || y + cell_height > SCREEN_HEIGHT) {
        return;
    }

    for (; *text != 0; ++text, x += cell_width) {
        if (x < 0 || x + cell_width > SCREEN_WIDTH) {
            continue;
        }
        if (scale == 4) {
            draw_cell_4x(buffer, x, y, glyph_for(*text), fg, bg);
        } else if (scale == 2) {
            draw_cell_2x(buffer, x, y, glyph_for(*text), fg, bg);
        } else {
            draw_cell_1x(buffer, x, y, glyph_for(*text), fg, bg);
        }
    }
}
