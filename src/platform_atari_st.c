#include "platform.h"

#include "st_text.h"

#include <stddef.h>
#include <stdio.h>
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

/* The playing field in 16-pixel groups and rows, for the rectangle clear. */
#define FIELD_GROUP0 (PLATFORM_FIELD_X / 16)
#define FIELD_GROUP1 ((PLATFORM_FIELD_X + PLATFORM_FIELD_WIDTH) / 16 - 1)
#define FIELD_Y0 PLATFORM_FIELD_Y
#define FIELD_Y1 (PLATFORM_FIELD_Y + PLATFORM_FIELD_HEIGHT - 1)
#define FIELD_X0 PLATFORM_FIELD_X
#define FIELD_X1 (PLATFORM_FIELD_X + PLATFORM_FIELD_WIDTH - 1)

#define SCORE_FILE "ASTROIDS.SCO"
#define MAX_DIRTY_RECTS 64

extern void st_clear_buffer(unsigned char *buffer);
extern void st_draw_line_low(unsigned char *buffer, long x0, long y0, long x1, long y1, long color);
extern void st_draw_line_plane(unsigned char *buffer, long x0, long y0, long x1, long y1, long plane_offset);
extern void st_draw_poly_plane(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_clear_rect(unsigned char *buffer, long group0, long group1, long y0, long y1);
extern void st_ikbd_install(void);
extern void st_ikbd_remove(void);

/* Written by the IKBD interrupt handler in st_ikbd.S: 1 while the key is held. */
volatile unsigned char st_key_state[128];

typedef struct DirtyRect {
    short x0;
    short y0;
    short x1;
    short y1;
} DirtyRect;

static unsigned char screen_storage[2][SCREEN_BYTES + 255];
static unsigned char *screen_pages[2];
static unsigned char *draw_buffer;
static unsigned char *show_buffer;
static void *original_physbase;
static void *original_logbase;
static int original_resolution;
static uint16_t original_palette[ST_PALETTE_COLORS];
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

static void set_palette(void) {
    /* Lovable "Classic" theme, quantised to the ST's 3 bits per channel. The line colours
       (ship, rocks) sit on single bitplanes (1, 2, 4, 8) for fast drawing. */
    static const uint16_t palette[ST_PALETTE_COLORS] = {
        0x000, 0x272, 0x777, 0x741,   /* black field, ship+bullets, large rock, thrust flame */
        0x467, 0x124, 0x770, 0x722,   /* medium rock, frame, yellow, red */
        0x247, 0x555, 0x757, 0x333,   /* small rock, grey */
        0x057, 0x444, 0x666, 0x111
    };
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

/* Fill a whole screen with the frame colour, then cut out the black playing field. */
static void draw_frame(unsigned char *buffer) {
    unsigned short *words = (unsigned short *) buffer;
    int index;
    int plane;

    for (index = 0; index < SCREEN_BYTES / 8; ++index) {
        for (plane = 0; plane < 4; ++plane) {
            *words++ = (unsigned short) ((((GAME_COLOR_FRAME >> plane) & 1) != 0) ? 0xffffu : 0u);
        }
    }
    st_clear_rect(buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
}

static int page_index(void) {
    return (draw_buffer == screen_pages[0]) ? 0 : 1;
}

static void enter_video(void) {
    if (Getrez() != 0) {
        Setscreen((void *) -1L, (void *) -1L, 0);
        wait_vbl();
        wait_vbl();
    }
    draw_frame(screen_pages[0]);
    draw_frame(screen_pages[1]);
    dirty_count[0] = 0;
    dirty_count[1] = 0;
    dirty_full[0] = 0;
    dirty_full[1] = 0;
    show_buffer = screen_pages[0];
    draw_buffer = screen_pages[1];
    show_screen(show_buffer);
    wait_vbl();
    set_palette();
    last_frame_clock = ST_FRCLOCK - 1;
}

int platform_init(void) {
    static const char ikbd_game_mode[] = { 0x12, 0x1a };  /* mouse off, joysticks off */
    int i;

    entered_supervisor = 0;
    if (Super((void *) 1L) == 0) {
        saved_ssp = Super((void *) 0L);
        entered_supervisor = 1;
    }

    if (Getrez() == 2) {
        (void) Cconws("Atari colour monitor required.\r\n");
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
    memset((void *) st_key_state, 0, sizeof(st_key_state));

    enter_video();
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

    input->left = (uint8_t) (st_key_state[0x1eu] || st_key_state[0x4bu]);        /* A, left */
    input->right = (uint8_t) (st_key_state[0x20u] || st_key_state[0x4du]);       /* D, right */
    input->thrust = (uint8_t) (st_key_state[0x11u] || st_key_state[0x48u]);      /* W, up */
    input->fire = st_key_state[0x39u];                                           /* space */
    input->hyperspace = st_key_state[0x23u];                                     /* H */
    input->start = (uint8_t) (st_key_state[0x39u] || st_key_state[0x1cu]);       /* space, return */
    input->pause = st_key_state[0x19u];                                          /* P */
    input->exit_requested = (uint8_t) (st_key_state[0x10u] || st_key_state[0x01u]);   /* Q, Esc */
}

void platform_begin_frame(void) {
    const int page = page_index();
    int index;

    if (dirty_full[page]) {
        st_clear_rect(draw_buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
    } else {
        for (index = 0; index < dirty_count[page]; ++index) {
            const DirtyRect *rect = &dirty_rects[page][index];
            st_clear_rect(draw_buffer, rect->x0 >> 4, rect->x1 >> 4, rect->y0, rect->y1);
        }
    }
    dirty_count[page] = 0;
    dirty_full[page] = 0;
}

void platform_clear_field(void *context) {
    (void) context;
    st_clear_rect(draw_buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
}

void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1) {
    const int page = page_index();
    DirtyRect *rect;

    (void) context;

    if (x0 < FIELD_X0) {
        x0 = FIELD_X0;
    }
    if (y0 < FIELD_Y0) {
        y0 = FIELD_Y0;
    }
    if (x1 > FIELD_X1) {
        x1 = FIELD_X1;
    }
    if (y1 > FIELD_Y1) {
        y1 = FIELD_Y1;
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

/* Cohen-Sutherland outcodes for the playing field (inclusive bounds). */
static int outcode(long x, long y) {
    int code = 0;

    if (x < FIELD_X0) {
        code |= 1;
    } else if (x > FIELD_X1) {
        code |= 2;
    }
    if (y < FIELD_Y0) {
        code |= 4;
    } else if (y > FIELD_Y1) {
        code |= 8;
    }
    return code;
}

/* Clip a line to the playing field. Returns 0 when nothing of it is visible. */
static int clip_line(long *x0, long *y0, long *x1, long *y1) {
    int code0 = outcode(*x0, *y0);
    int code1 = outcode(*x1, *y1);
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
            y = FIELD_Y1;
            x = *x0 + ((*x1 - *x0) * (y - *y0)) / (*y1 - *y0);
        } else if (code & 4) {
            y = FIELD_Y0;
            x = *x0 + ((*x1 - *x0) * (y - *y0)) / (*y1 - *y0);
        } else if (code & 2) {
            x = FIELD_X1;
            y = *y0 + ((*y1 - *y0) * (x - *x0)) / (*x1 - *x0);
        } else {
            x = FIELD_X0;
            y = *y0 + ((*y1 - *y0) * (x - *x0)) / (*x1 - *x0);
        }

        if (code == code0) {
            *x0 = x;
            *y0 = y;
            code0 = outcode(x, y);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = outcode(x, y);
        }
    }
    return 0;
}

static int inside_field(int x, int y) {
    return x >= FIELD_X0 && x <= FIELD_X1 && y >= FIELD_Y0 && y <= FIELD_Y1;
}

static long plane_offset_for(uint8_t color) {
    return (color == 1) ? 0 : (color == 2) ? 2 : (color == 4) ? 4 : 6;
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    long cx0 = x0;
    long cy0 = y0;
    long cx1 = x1;
    long cy1 = y1;

    (void) context;

    if (!inside_field(x0, y0) || !inside_field(x1, y1)) {
        if (!clip_line(&cx0, &cy0, &cx1, &cy1)) {
            return;
        }
    }

    if (color == 1 || color == 2 || color == 4 || color == 8) {
        st_draw_line_plane(draw_buffer, cx0, cy0, cx1, cy1, plane_offset_for(color));
    } else {
        st_draw_line_low(draw_buffer, cx0, cy0, cx1, cy1, color);
    }
}

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    int index;

    if (color == 1 || color == 2 || color == 4 || color == 8) {
        int inside = 1;

        for (index = 0; index < count; ++index) {
            if (!inside_field(points[index * 2], points[index * 2 + 1])) {
                inside = 0;
                break;
            }
        }
        if (inside) {
            st_draw_poly_plane(draw_buffer, points, count, plane_offset_for(color));
            return;
        }
    }

    /* partly off the field, or a mixed-plane colour: edge by edge, with clipping */
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        platform_draw_line(context, points[index * 2], points[index * 2 + 1], points[next * 2],
                           points[next * 2 + 1], color);
    }
}

void platform_draw_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    (void) context;
    st_text_draw(draw_buffer, x, y, text, fg, bg, scale);
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

int platform_load_scores(uint8_t *data, int size) {
    FILE *file = fopen(SCORE_FILE, "rb");
    int count = 0;

    if (file != NULL) {
        count = (int) fread(data, 1, (size_t) size, file);
        fclose(file);
    }
    return count;
}

void platform_save_scores(const uint8_t *data, int size) {
    FILE *file = fopen(SCORE_FILE, "wb");

    if (file != NULL) {
        (void) fwrite(data, 1, (size_t) size, file);
        fclose(file);
    }
}

#else
int platform_init(void) {
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

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    (void) points;
    (void) count;
    (void) color;
}

void platform_draw_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    (void) context;
    (void) x;
    (void) y;
    (void) text;
    (void) fg;
    (void) bg;
    (void) scale;
}

void platform_clear_field(void *context) {
    (void) context;
}

void platform_end_frame(void) {
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

int platform_load_scores(uint8_t *data, int size) {
    (void) data;
    (void) size;
    return 0;
}

void platform_save_scores(const uint8_t *data, int size) {
    (void) data;
    (void) size;
}
#endif
