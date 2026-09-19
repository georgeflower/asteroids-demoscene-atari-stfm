#include "platform.h"

#include <stddef.h>
#include <string.h>

#if defined(ATARI_ST_TARGET)
#include <osbind.h>

#define SCREEN_BYTES 32000
#define ST_PALETTE_COLORS 16
#define IKBD_DEVICE 4

extern void st_clear_buffer(unsigned char *buffer);
extern void st_draw_line_low(unsigned char *buffer, long x0, long y0, long x1, long y1, long color);

static unsigned char screen_storage[2][SCREEN_BYTES + 255];
static unsigned char *screen_pages[2];
static unsigned char *draw_buffer;
static unsigned char *show_buffer;
static void *original_physbase;
static void *original_logbase;
static int original_resolution;
static short original_palette[ST_PALETTE_COLORS];
static PlatformConfig current_config;
static unsigned char key_state[128];
static unsigned char ikbd_packet_remaining;
static unsigned char previous_toggle_state;

static unsigned char *aligned_screen(int index) {
    unsigned long address = (unsigned long) screen_storage[index];
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
    st_clear_buffer(draw_buffer);
}

static void clear_all_screens(void) {
    st_clear_buffer(screen_pages[0]);
    st_clear_buffer(screen_pages[1]);
}

static void update_key_state(unsigned char code) {
    if (ikbd_packet_remaining > 0) {
        --ikbd_packet_remaining;
        return;
    }

    if (code >= 0xf6u) {
        if (code <= 0xf7u) {
            ikbd_packet_remaining = 5;
        } else if (code <= 0xfbu) {
            ikbd_packet_remaining = 2;
        } else if (code <= 0xfdu) {
            ikbd_packet_remaining = 6;
        } else {
            ikbd_packet_remaining = 1;
        }
        return;
    }

    key_state[code & 0x7fu] = (unsigned char) ((code & 0x80u) == 0);
}

static void plot_pixel(int x, int y, uint8_t color) {
    const int words_per_group = (current_config.resolution == PLATFORM_RES_MEDIUM) ? 2 : 4;
    const int row_offset = y * 160;
    const int group_offset = (x >> 4) * words_per_group * 2;
    const int bit = 15 - (x & 15);
    unsigned short mask = (unsigned short) (1u << bit);
    unsigned short *words = (unsigned short *) (draw_buffer + row_offset + group_offset);
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

    screen_pages[0] = aligned_screen(0);
    screen_pages[1] = aligned_screen(1);
    show_buffer = screen_pages[0];
    draw_buffer = screen_pages[1];
    current_config = *config;
    memset(key_state, 0, sizeof(key_state));
    ikbd_packet_remaining = 0;
    previous_toggle_state = 0;
    clear_all_screens();
    Setscreen((void *) show_buffer, (void *) show_buffer, config->resolution);
    set_palette(config);
    return 1;
}

void platform_shutdown(void) {
    int i;

    Setscreen((void *) original_logbase, (void *) original_physbase, original_resolution);
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        (void) Setcolor(i, original_palette[i]);
    }
}

void platform_poll_input(GameInput *input) {
    memset(input, 0, sizeof(*input));

    while (Bconstat(IKBD_DEVICE)) {
        update_key_state((unsigned char) (Bconin(IKBD_DEVICE) & 0xffL));
    }

    input->left = (uint8_t) (key_state[0x1eu] || key_state[0x4bu]);
    input->right = (uint8_t) (key_state[0x20u] || key_state[0x4du]);
    input->thrust = (uint8_t) (key_state[0x11u] || key_state[0x48u]);
    input->fire = key_state[0x39u];
    {
        const unsigned char toggle_state = (unsigned char) (key_state[0x32u] || key_state[0x0fu] || key_state[0x3fu]);
        input->toggle_resolution = (uint8_t) (toggle_state && !previous_toggle_state);
        previous_toggle_state = toggle_state;
    }
    input->exit_requested = (uint8_t) (key_state[0x10u] || key_state[0x01u]);
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

    if (current_config.resolution == PLATFORM_RES_LOW && color != 0) {
        st_draw_line_low(draw_buffer, x0, y0, x1, y1, color);
        return;
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
    Setscreen((void *) draw_buffer, (void *) draw_buffer, -1);
    if (show_buffer == screen_pages[0]) {
        show_buffer = screen_pages[1];
        draw_buffer = screen_pages[0];
    } else {
        show_buffer = screen_pages[0];
        draw_buffer = screen_pages[1];
    }
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
    show_buffer = screen_pages[0];
    draw_buffer = screen_pages[1];
    previous_toggle_state = 0;
    clear_all_screens();
    Setscreen((void *) show_buffer, (void *) show_buffer, config->resolution);
    set_palette(config);
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
