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

void st_text_draw(unsigned char *buffer, int x, int y, const char *text, int fg, int bg, int scale) {
    const int cell_width = (scale == 2) ? 16 : 8;
    const int cell_height = (scale == 2) ? 16 : 8;

    x -= x % cell_width;
    if (y < 0 || y + cell_height > SCREEN_HEIGHT) {
        return;
    }

    for (; *text != 0; ++text, x += cell_width) {
        if (x < 0 || x + cell_width > SCREEN_WIDTH) {
            continue;
        }
        if (scale == 2) {
            draw_cell_2x(buffer, x, y, glyph_for(*text), fg, bg);
        } else {
            draw_cell_1x(buffer, x, y, glyph_for(*text), fg, bg);
        }
    }
}
