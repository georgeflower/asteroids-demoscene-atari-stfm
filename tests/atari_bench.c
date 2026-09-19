/*
 * Frame cost benchmark for the Atari build. Times the parts of a frame
 * (game step, screen clear, render maths, line drawing) separately for a few
 * wave sizes and prints milliseconds per frame. A frame must fit in 20 ms to
 * hold 50 fps. Run it inside Hatari: make bench-atari.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define BENCH_FRAMES 50
#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)

enum {
    MODE_STEP,
    MODE_MATHS_AND_CLEAR,
    MODE_RENDER_MATHS,
    MODE_RENDER_DRAW,
    MODE_ALL,
    MODE_COUNT
};

static uint32_t lines_seen;

static void null_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) color;
    ++lines_seen;
}

static uint32_t run(GameState *state, int mode) {
    GameInput input;
    uint32_t start;
    int frame;

    memset(&input, 0, sizeof(input));
    input.left = 1;   /* manual input: measures the game, not the demo autopilot */
    lines_seen = 0;
    start = ST_HZ200;
    for (frame = 0; frame < BENCH_FRAMES; ++frame) {
        state->ship.invulnerability = 255;
        if (mode == MODE_STEP || mode == MODE_ALL) {
            game_step(state, &input);
        }
        if (mode == MODE_MATHS_AND_CLEAR) {
            /* render without drawing, only to report the dirty areas, then erase them */
            game_render(state, NULL, null_line, NULL, platform_mark_dirty);
            platform_begin_frame();
        }
        if (mode == MODE_ALL) {
            platform_begin_frame();
        }
        if (mode == MODE_RENDER_MATHS) {
            game_render(state, NULL, null_line, NULL, NULL);
        }
        if (mode == MODE_RENDER_DRAW || mode == MODE_ALL) {
            game_render(state, NULL, platform_draw_line, platform_draw_polygon, platform_mark_dirty);
        }
    }
    return ST_HZ200 - start;
}

static int count_rocks(const GameState *state) {
    int rocks = 0;
    int index;

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        rocks += state->asteroids[index].active;
    }
    return rocks;
}

int main(void) {
    static const int waves[3] = {1, 4, 8};
    static const char *const names[MODE_COUNT] = {"game_step", "maths+clear", "render maths", "render+draw", "all"};
    PlatformConfig config;
    GameState state;
    uint32_t ticks[3][MODE_COUNT];
    int rocks[3];
    uint32_t lines_per_frame = 0;
    int wave_index;
    int mode;

    config.resolution = PLATFORM_RES_LOW;
    config.width = 320;
    config.height = 200;
    if (!platform_init(&config)) {
        return 1;
    }

    for (wave_index = 0; wave_index < 3; ++wave_index) {
        game_init(&state, config.width, config.height);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.wave = (uint8_t) (waves[wave_index] - 1);
        state.ship.invulnerability = 255;
        game_step(&state, &(GameInput) {0, 0, 0, 0, 0, 0});
        rocks[wave_index] = count_rocks(&state);
        for (mode = 0; mode < MODE_COUNT; ++mode) {
            ticks[wave_index][mode] = run(&state, mode);
            if (mode == MODE_RENDER_MATHS && wave_index == 2) {
                lines_per_frame = lines_seen / BENCH_FRAMES;
            }
        }
    }

    platform_shutdown();

    for (wave_index = 0; wave_index < 3; ++wave_index) {
        printf("wave %d (%d rocks):\n", waves[wave_index], rocks[wave_index]);
        for (mode = 0; mode < MODE_COUNT; ++mode) {
            /* 200 Hz ticks: ms per frame = ticks * 5 / frames, shown with one decimal */
            const unsigned long tenths = (unsigned long) (ticks[wave_index][mode] * 50 / BENCH_FRAMES);
            printf("  %-13s %lu.%lu ms\n", names[mode], tenths / 10, tenths % 10);
        }
    }
    printf("20.0 ms = 50 fps. lines/frame at wave 8: %lu\n", (unsigned long) lines_per_frame);
    printf("press a key\n");
    getchar();
    return 0;
}
