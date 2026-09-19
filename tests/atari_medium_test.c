/*
 * Medium-resolution smoke test for the ST: prints the geometry the game hands to the
 * drawer (in text mode, before touching the video hardware), then runs some frames of
 * the real loop at 640x200 and reports that it got through. Run in Hatari:
 * make test-medium-atari.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#ifndef MEDIUM_TEST_FRAMES
#define MEDIUM_TEST_FRAMES 300
#endif

/* progress log on the emulated hard drive (a host folder), readable even if the machine dies */
static void log_step(const char *text) {
    static const char path[] = {'C', ':', 92, 'M', 'E', 'D', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");
    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static int seen_polygons;
static int first_points[4][24];
static int first_counts[4];
static int first_colors[4];

static void remember_polygon(const int16_t *points, int count, uint8_t color) {
    if (seen_polygons < 4) {
        int index;
        for (index = 0; index < count * 2 && index < 24; ++index) {
            first_points[seen_polygons][index] = points[index];
        }
        first_counts[seen_polygons] = count;
        first_colors[seen_polygons] = color;
    }
    ++seen_polygons;
}

static void dump_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    remember_polygon(points, count, color);
}

static void ignore_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) color;
}

static int tracing;

static void traced_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    if (tracing) {
        char text[120];
        int index;
        int length = sprintf(text, "polygon n=%d c=%d:", count, (int) color);
        for (index = 0; index < count * 2 && index < 8; ++index) {
            length += sprintf(text + length, " %d", (int) points[index]);
        }
        log_step(text);
    }
    platform_draw_polygon(context, points, count, color);
    if (tracing) {
        log_step("polygon done");
    }
}

static void traced_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    if (tracing) {
        char text[80];
        sprintf(text, "line %d,%d -> %d,%d c=%d", x0, y0, x1, y1, (int) color);
        log_step(text);
    }
    platform_draw_line(context, x0, y0, x1, y1, color);
}

static void traced_dirty(void *context, int x0, int y0, int x1, int y1) {
    if (tracing) {
        char text[80];
        sprintf(text, "dirty %d,%d %d,%d", x0, y0, x1, y1);
        log_step(text);
    }
    platform_mark_dirty(context, x0, y0, x1, y1);
}

int main(void) {
    PlatformConfig config;
    GameState state;
    GameInput input;
    int frame;
    int shape;
    int index;

    log_step("start");
    game_init(&state, 640, 200);
    log_step("game_init ok");
    memset(&input, 0, sizeof(input));
    game_step(&state, &input);
    log_step("game_step ok");
    seen_polygons = 0;
    game_render(&state, NULL, ignore_line, dump_polygon, NULL);
    log_step("game_render ok");
    printf("scales %u %u, size %ux%u\n", (unsigned) state.x_scale, (unsigned) state.y_scale,
           (unsigned) state.width, (unsigned) state.height);
    for (shape = 0; shape < 3; ++shape) {
        printf("poly %d n=%d c=%d:", shape, first_counts[shape], first_colors[shape]);
        for (index = 0; index < first_counts[shape] * 2 && index < 12; ++index) {
            printf(" %d", first_points[shape][index]);
        }
        printf("\n");
    }
    printf("press a key for the graphics part\n");
    getchar();

    log_step("entering graphics");
    config.resolution = PLATFORM_RES_MEDIUM;
    config.width = 640;
    config.height = 200;
    if (!platform_init(&config)) {
        return 1;
    }

    log_step("platform_init ok");
    game_init(&state, config.width, config.height);
    memset(&input, 0, sizeof(input));
    input.thrust = 1;
    input.fire = 1;
    for (frame = 0; frame < MEDIUM_TEST_FRAMES; ++frame) {
        int steps = platform_take_elapsed_frames();
        while (steps-- > 0) {
            game_step(&state, &input);
        }
        if (frame < 5) {
            log_step("frame begin");
        }
        platform_begin_frame();
        if (frame < 5) {
            log_step("cleared");
        }
        tracing = (frame == 0);
        game_render(&state, NULL, traced_line, traced_polygon, traced_dirty);
        tracing = 0;
        if (frame < 5) {
            log_step("rendered");
        }
        platform_end_frame();
        if (frame < 5) {
            log_step("flipped");
        }
    }

    platform_shutdown();
    printf("medium resolution: %d frames ok\n", MEDIUM_TEST_FRAMES);
    printf("press a key\n");
    getchar();
    return 0;
}
