#include "platform.h"

#include <stddef.h>
#include <string.h>

#if defined(ATARI_ST_TARGET)
#include <osbind.h>

#define SCREEN_BYTES 32000
#define ST_PALETTE_COLORS 16

static unsigned char screen_storage[SCREEN_BYTES + 255];
static unsigned char *screen_base;
static long original_physbase;
static long original_logbase;
static int original_resolution;
static short original_palette[ST_PALETTE_COLORS];
static PlatformConfig current_config;

static unsigned char *aligned_screen(void) {
    unsigned long address = (unsigned long) screen_storage;
    address = (address + 255u) & ~255u;
    return (unsigned char *) address;
}

static void set_palette(const PlatformConfig *config) {
    static short low_palette[ST_PALETTE_COLORS] = {
        0x000, 0x777, 0x420, 0x530,
        0x640, 0x750, 0x770, 0x333,
        0x444, 0x555, 0x666, 0x222,
        0x111, 0x210, 0x431, 0x764
    };
    static short medium_palette[ST_PALETTE_COLORS] = {
        0x000, 0x777, 0x555, 0x333,
        0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000
    };

    Setpalette((config->resolution == PLATFORM_RES_MEDIUM) ? medium_palette : low_palette);
}

static void clear_screen(void) {
    memset(screen_base, 0, SCREEN_BYTES);
}

static void plot_pixel(int x, int y, uint8_t color) {
    const int words_per_group = (current_config.resolution == PLATFORM_RES_MEDIUM) ? 2 : 4;
    const int row_offset = y * 160;
    const int group_offset = (x >> 4) * words_per_group * 2;
    const int bit = 15 - (x & 15);
    unsigned short mask = (unsigned short) (1u << bit);
    unsigned short *words = (unsigned short *) (screen_base + row_offset + group_offset);
    int plane_count = (current_config.resolution == PLATFORM_RES_MEDIUM) ? 2 : 4;
    int plane;

    for (plane = 0; plane < plane_count; ++plane) {
        if ((color >> plane) & 1u) {
            words[plane] |= mask;
        } else {
            words[plane] &= (unsigned short) ~mask;
        }
    }
}

int platform_init(const PlatformConfig *config) {
    int i;

    original_physbase = Physbase();
    original_logbase = Logbase();
    original_resolution = Getrez();
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        original_palette[i] = Setcolor(i, -1);
    }

    screen_base = aligned_screen();
    current_config = *config;
    Setscreen((void *) screen_base, (void *) screen_base, config->resolution);
    set_palette(config);
    clear_screen();
    return 1;
}

void platform_shutdown(void) {
    int i;

    Setscreen((void *) original_logbase, (void *) original_physbase, original_resolution);
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        Setcolor(i, original_palette[i]);
    }
}

void platform_poll_input(GameInput *input) {
    memset(input, 0, sizeof(*input));

    while (Cconis()) {
        const long raw = Crawcin();
        const int ascii = (int) (raw & 0xffL);
        const int scan = (int) ((raw >> 16) & 0xffL);

        switch (ascii) {
            case 'a':
            case 'A':
                input->left = 1;
                break;
            case 'd':
            case 'D':
                input->right = 1;
                break;
            case 'w':
            case 'W':
                input->thrust = 1;
                break;
            case ' ':
                input->fire = 1;
                break;
            case 'm':
            case 'M':
            case '\t':
                input->toggle_resolution = 1;
                break;
            case 'q':
            case 'Q':
            case 27:
                input->exit_requested = 1;
                break;
        }

        switch (scan) {
            case 0x4b:
                input->left = 1;
                break;
            case 0x4d:
                input->right = 1;
                break;
            case 0x48:
                input->thrust = 1;
                break;
            case 0x39:
                input->fire = 1;
                break;
            case 0x3f:
                input->toggle_resolution = 1;
                break;
            case 0x01:
                input->exit_requested = 1;
                break;
        }
    }
}

void platform_begin_frame(void) {
    clear_screen();
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx;
    int sx;
    int dy;
    int sy;
    int err;

    (void) context;

    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x0 >= current_config.width) {
        x0 = current_config.width - 1;
    }
    if (x1 >= current_config.width) {
        x1 = current_config.width - 1;
    }
    if (y0 >= current_config.height) {
        y0 = current_config.height - 1;
    }
    if (y1 >= current_config.height) {
        y1 = current_config.height - 1;
    }

    dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    sx = (x0 < x1) ? 1 : -1;
    dy = (y0 < y1) ? -(y1 - y0) : -(y0 - y1);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        plot_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void platform_end_frame(void) {
    Vsync();
}

int platform_cycle_resolution(PlatformConfig *config) {
    if (config->resolution == PLATFORM_RES_LOW) {
        config->resolution = PLATFORM_RES_MEDIUM;
        config->width = 640;
        config->height = 200;
    } else {
        config->resolution = PLATFORM_RES_LOW;
        config->width = 320;
        config->height = 200;
    }

    current_config = *config;
    Setscreen((void *) screen_base, (void *) screen_base, config->resolution);
    set_palette(config);
    clear_screen();
    return 1;
}

#else
int platform_init(const PlatformConfig *config) {
    (void) config;
    return 0;
}

void platform_shutdown(void) {
}

void platform_poll_input(GameInput *input) {
    memset(input, 0, sizeof(*input));
}

void platform_begin_frame(void) {
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) color;
}

void platform_end_frame(void) {
}

int platform_cycle_resolution(PlatformConfig *config) {
    (void) config;
    return 0;
}
#endif
