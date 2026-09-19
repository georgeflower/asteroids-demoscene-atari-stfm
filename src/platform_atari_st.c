#include "platform.h"

#include <stddef.h>
#include <string.h>

#if defined(ATARI_ST_TARGET)
#include <osbind.h>

#define SCREEN_BYTES 32000
#define ST_PALETTE_COLORS 16

#define ST_FRCLOCK (*(volatile uint32_t *) 0x466UL)
#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define ST_VIDEO_BASE_HIGH (*(volatile uint8_t *) 0xff8201UL)
#define ST_VIDEO_BASE_MID (*(volatile uint8_t *) 0xff8203UL)
#define ST_HW_PALETTE ((volatile uint16_t *) 0xff8240UL)

extern void st_clear_buffer(unsigned char *buffer);
extern void st_draw_line_low(unsigned char *buffer, long x0, long y0, long x1, long y1, long color);
extern void st_draw_line_plane(unsigned char *buffer, long x0, long y0, long x1, long y1, long plane_offset);
extern void st_draw_poly_plane(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_clear_rect(unsigned char *buffer, long group0, long group1, long y0, long y1);
extern void st_ikbd_install(void);
extern void st_ikbd_remove(void);

/* Written by the IKBD interrupt handler in st_ikbd.S: 1 while the key is held. */
volatile unsigned char st_key_state[128];

static unsigned char screen_storage[2][SCREEN_BYTES + 255];
static unsigned char *screen_pages[2];
static unsigned char *draw_buffer;
static unsigned char *show_buffer;
static void *original_physbase;
static void *original_logbase;
static int original_resolution;
static uint16_t original_palette[ST_PALETTE_COLORS];
static PlatformConfig current_config;
static unsigned char previous_toggle_state;
#define MAX_DIRTY_RECTS 64

typedef struct DirtyRect {
    short x0;
    short y0;
    short x1;
    short y1;
} DirtyRect;

/* Per screen page: what was drawn on it last time, so only that gets erased. */
static DirtyRect dirty_rects[2][MAX_DIRTY_RECTS];
static int dirty_count[2];
static unsigned char dirty_full[2];
static uint32_t last_frame_clock;
static long saved_ssp;
static int entered_supervisor;

static unsigned char *aligned_screen(int index) {
    unsigned long address = (unsigned long) screen_storage[index];
    address = (address + 255u) & ~255u;
    return (unsigned char *) address;
}

/* Wait for the next vertical blank; give up after ~50 ms so a dead VBL cannot hang the machine. */
static void wait_vbl(void) {
    const uint32_t frame = ST_FRCLOCK;
    const uint32_t start = ST_HZ200;

    while (ST_FRCLOCK == frame && (ST_HZ200 - start) < 10u) {
    }
}

static void set_palette(const PlatformConfig *config) {
    /* Lovable "Classic" theme, quantised to the ST's 3 bits per channel. The
       line colours sit on single bitplanes (1, 2, 4, 8) for fast drawing. */
    static const uint16_t low_palette[ST_PALETTE_COLORS] = {
        0x001, 0x272, 0x777, 0x741,   /* background, ship+bullets, large rock, thrust flame */
        0x467, 0x555, 0x333, 0x777,   /* medium rock */
        0x247, 0x555, 0x666, 0x222,   /* small rock */
        0x111, 0x210, 0x431, 0x764
    };
    static const uint16_t medium_palette[ST_PALETTE_COLORS] = {
        0x001, 0x272, 0x777, 0x467,
        0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000
    };
    const uint16_t *palette = (config->resolution == PLATFORM_RES_MEDIUM) ? medium_palette : low_palette;
    int i;

    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        ST_HW_PALETTE[i] = palette[i];
    }
}

static void show_screen(const unsigned char *buffer) {
    const unsigned long address = (unsigned long) buffer;

    ST_VIDEO_BASE_HIGH = (uint8_t) (address >> 16);
    ST_VIDEO_BASE_MID = (uint8_t) (address >> 8);
}

static void clear_screen(void) {
    st_clear_buffer(draw_buffer);
}

static void clear_all_screens(void) {
    st_clear_buffer(screen_pages[0]);
    st_clear_buffer(screen_pages[1]);
}

/* Switch resolution through XBIOS, then take over the display registers. */
static void enter_resolution(const PlatformConfig *config) {
    int current = Getrez();

    if (current != config->resolution) {
        Setscreen((void *) -1L, (void *) -1L, config->resolution);
        wait_vbl();
        wait_vbl();
    }
    clear_all_screens();
    dirty_count[0] = 0;
    dirty_count[1] = 0;
    dirty_full[0] = 0;
    dirty_full[1] = 0;
    show_buffer = screen_pages[0];
    draw_buffer = screen_pages[1];
    show_screen(show_buffer);
    wait_vbl();
    set_palette(config);
    last_frame_clock = ST_FRCLOCK - 1;
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
    static const char ikbd_game_mode[] = { 0x12, 0x1a };  /* mouse off, joysticks off */
    int i;

    entered_supervisor = 0;
    if (Super((void *) 1L) == 0) {
        saved_ssp = Super((void *) 0L);
        entered_supervisor = 1;
    }

    if (Getrez() == 2) {
        (void) Cconws("Atari colour monitor required (low or medium resolution).\r\n");
        if (entered_supervisor) {
            (void) Super((void *) saved_ssp);
            entered_supervisor = 0;
        }
        return 0;
    }

    original_physbase = Physbase();
    original_logbase = Logbase();
    original_resolution = Getrez();
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        original_palette[i] = ST_HW_PALETTE[i];
    }

    screen_pages[0] = aligned_screen(0);
    screen_pages[1] = aligned_screen(1);
    current_config = *config;
    previous_toggle_state = 0;
    memset((void *) st_key_state, 0, sizeof(st_key_state));

    enter_resolution(config);
    (void) Ikbdws(1, ikbd_game_mode);
    st_ikbd_install();
    return 1;
}

void platform_shutdown(void) {
    static const char ikbd_tos_mode[] = { 0x08, 0x14 };  /* relative mouse, joystick events */
    int i;

    if (!entered_supervisor) {
        return;
    }

    st_ikbd_remove();
    (void) Ikbdws(1, ikbd_tos_mode);
    Setscreen(original_logbase, original_physbase, original_resolution);
    wait_vbl();
    wait_vbl();
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        ST_HW_PALETTE[i] = original_palette[i];
    }
    (void) Super((void *) saved_ssp);
    entered_supervisor = 0;
}

void platform_poll_input(GameInput *input) {
    memset(input, 0, sizeof(*input));

    input->left = (uint8_t) (st_key_state[0x1eu] || st_key_state[0x4bu]);
    input->right = (uint8_t) (st_key_state[0x20u] || st_key_state[0x4du]);
    input->thrust = (uint8_t) (st_key_state[0x11u] || st_key_state[0x48u]);
    input->fire = st_key_state[0x39u];
    {
        const unsigned char toggle_state = (unsigned char) (st_key_state[0x32u] || st_key_state[0x0fu] || st_key_state[0x3fu]);
        input->toggle_resolution = (uint8_t) (toggle_state && !previous_toggle_state);
        previous_toggle_state = toggle_state;
    }
    input->exit_requested = (uint8_t) (st_key_state[0x10u] || st_key_state[0x01u]);
}

void platform_begin_frame(void) {
    const int page = (draw_buffer == screen_pages[0]) ? 0 : 1;
    int index;

    if (current_config.resolution != PLATFORM_RES_LOW || dirty_full[page]) {
        clear_screen();
    } else {
        for (index = 0; index < dirty_count[page]; ++index) {
            const DirtyRect *rect = &dirty_rects[page][index];
            st_clear_rect(draw_buffer, rect->x0 >> 4, rect->x1 >> 4, rect->y0, rect->y1);
        }
    }
    dirty_count[page] = 0;
    dirty_full[page] = 0;
}

void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1) {
    const int page = (draw_buffer == screen_pages[0]) ? 0 : 1;
    DirtyRect *rect;

    (void) context;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= current_config.width) {
        x1 = current_config.width - 1;
    }
    if (y1 >= current_config.height) {
        y1 = current_config.height - 1;
    }
    if (x0 > x1 || y0 > y1) {
        return;
    }
    if (dirty_count[page] >= MAX_DIRTY_RECTS) {
        dirty_full[page] = 1;
        return;
    }

    rect = &dirty_rects[page][dirty_count[page]++];
    rect->x0 = (short) x0;
    rect->y0 = (short) y0;
    rect->x1 = (short) x1;
    rect->y1 = (short) y1;
}

/* Vertical blanks since the last call (1 when the frame kept up), so the game can step once per blank. */
int platform_take_elapsed_frames(void) {
    const uint32_t now = ST_FRCLOCK;
    uint32_t elapsed = now - last_frame_clock;

    last_frame_clock = now;
    if (elapsed < 1u) {
        elapsed = 1u;
    }
    if (elapsed > 4u) {
        elapsed = 4u;
    }
    return (int) elapsed;
}

/* Cohen-Sutherland outcodes for the inclusive rectangle [0, width-1] x [0, height-1]. */
static int outcode(long x, long y, long width, long height) {
    int code = 0;

    if (x < 0) {
        code |= 1;
    } else if (x >= width) {
        code |= 2;
    }
    if (y < 0) {
        code |= 4;
    } else if (y >= height) {
        code |= 8;
    }
    return code;
}

/* Clip a line to the screen. Returns 0 when nothing of it is visible. */
static int clip_line(long *x0, long *y0, long *x1, long *y1, long width, long height) {
    int code0 = outcode(*x0, *y0, width, height);
    int code1 = outcode(*x1, *y1, width, height);
    int guard;

    for (guard = 0; guard < 8; ++guard) {
        const int code = code0 ? code0 : code1;
        long x = 0;
        long y = 0;

        if ((code0 | code1) == 0) {
            return 1;
        }
        if ((code0 & code1) != 0) {
            return 0;
        }

        if (code & 8) {
            y = height - 1;
            x = *x0 + ((*x1 - *x0) * (y - *y0)) / (*y1 - *y0);
        } else if (code & 4) {
            y = 0;
            x = *x0 + ((*x1 - *x0) * (y - *y0)) / (*y1 - *y0);
        } else if (code & 2) {
            x = width - 1;
            y = *y0 + ((*y1 - *y0) * (x - *x0)) / (*x1 - *x0);
        } else {
            x = 0;
            y = *y0 + ((*y1 - *y0) * (x - *x0)) / (*x1 - *x0);
        }

        if (code == code0) {
            *x0 = x;
            *y0 = y;
            code0 = outcode(x, y, width, height);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = outcode(x, y, width, height);
        }
    }
    return 0;
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    long cx0 = x0;
    long cy0 = y0;
    long cx1 = x1;
    long cy1 = y1;
    int dx;
    int sx;
    int dy;
    int sy;
    int err;

    (void) context;

    if ((unsigned) x0 >= current_config.width || (unsigned) x1 >= current_config.width ||
        (unsigned) y0 >= current_config.height || (unsigned) y1 >= current_config.height) {
        if (!clip_line(&cx0, &cy0, &cx1, &cy1, current_config.width, current_config.height)) {
            return;
        }
    }

    if (current_config.resolution == PLATFORM_RES_LOW) {
        if (color == 1 || color == 2 || color == 4 || color == 8) {
            const long plane_offset = (color == 1) ? 0 : (color == 2) ? 2 : (color == 4) ? 4 : 6;
            st_draw_line_plane(draw_buffer, cx0, cy0, cx1, cy1, plane_offset);
        } else {
            st_draw_line_low(draw_buffer, cx0, cy0, cx1, cy1, color);
        }
        return;
    }

    /* Medium resolution only has four colours: fold the single-plane colours onto them. */
    if (color > 2) {
        color = 3;
    }

    x0 = (int) cx0;
    y0 = (int) cy0;
    x1 = (int) cx1;
    y1 = (int) cy1;
    dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    sx = (x0 < x1) ? 1 : -1;
    dy = (y0 < y1) ? -(y1 - y0) : -(y0 - y1);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        /* one error value for both tests: testing again after err changed can step past the end point */
        const int doubled_error = 2 * err;

        plot_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (doubled_error >= dy) {
            err += dy;
            x0 += sx;
        }
        if (doubled_error <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    int index;

    if (current_config.resolution == PLATFORM_RES_LOW && (color == 1 || color == 2 || color == 4 || color == 8)) {
        int inside = 1;

        for (index = 0; index < count; ++index) {
            if ((unsigned) points[index * 2] >= current_config.width ||
                (unsigned) points[index * 2 + 1] >= current_config.height) {
                inside = 0;
                break;
            }
        }
        if (inside) {
            const long plane_offset = (color == 1) ? 0 : (color == 2) ? 2 : (color == 4) ? 4 : 6;
            st_draw_poly_plane(draw_buffer, points, count, plane_offset);
            return;
        }
    }

    /* off-screen parts, medium resolution or a mixed-plane colour: edge by edge, with clipping */
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        platform_draw_line(context, points[index * 2], points[index * 2 + 1], points[next * 2],
                           points[next * 2 + 1], color);
    }
}

void platform_end_frame(void) {
    unsigned char *finished = draw_buffer;

    /* The ST only reloads the screen address at the vertical blank, so the old
       page stays on screen until then: request the flip, wait for the blank that
       carries it out, and only then draw into the page that was on screen. */
    show_screen(finished);
    wait_vbl();
    draw_buffer = show_buffer;
    show_buffer = finished;
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
    previous_toggle_state = 1;
    enter_resolution(config);
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

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    (void) points;
    (void) count;
    (void) color;
}

void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
}

int platform_take_elapsed_frames(void) {
    return 1;
}

int platform_cycle_resolution(PlatformConfig *config) {
    (void) config;
    return 0;
}
#endif
