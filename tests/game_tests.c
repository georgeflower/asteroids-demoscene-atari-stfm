#include "game.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition) \
    do { \
        ++checks; \
        if (!(condition)) { \
            ++failures; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        } \
    } while (0)

static const GameInput no_input = {0, 0, 0, 0, 0, 0};

static int count_asteroids(const GameState *state, int size) {
    int active = 0;
    int index;

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state->asteroids[index].active && (size == 0 || state->asteroids[index].size == size)) {
            ++active;
        }
    }
    return active;
}

/* Remove every rock except one parked in a far corner so the wave does not end. */
static void clear_field(GameState *state) {
    memset(state->asteroids, 0, sizeof(state->asteroids));
    memset(state->bullets, 0, sizeof(state->bullets));
    state->asteroids[0].active = 1;
    state->asteroids[0].size = GAME_ASTEROID_SMALL;
    state->asteroids[0].point_count = 8;
    state->asteroids[0].x = 5L << GAME_FIX_SHIFT;
    state->asteroids[0].y = 5L << GAME_FIX_SHIFT;
}

static void test_initial_wave(void) {
    GameState state;

    game_init(&state, 320, 200);

    CHECK(state.width == 320);
    CHECK(state.height == 200);
    CHECK(state.x_scale == 256);
    CHECK(state.y_scale == 213);
    CHECK(state.wave == 1);
    CHECK(state.lives == 3);
    CHECK(count_asteroids(&state, 0) == 6);
    CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == 6);
    CHECK(state.ship.angle == 49152u);
    CHECK(state.ship.invulnerability == 100);
}

static void test_wave_sizes(void) {
    GameState state;
    int wave;
    const int expected[] = {0, 6, 8, 10, 12, 14, 16, 18, 20, 20, 20};

    game_init(&state, 320, 200);
    for (wave = 1; wave <= 10; ++wave) {
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.wave = (uint8_t) (wave - 1);
        clear_field(&state);
        state.asteroids[0].active = 0;
        state.ship.invulnerability = 100;
        game_step(&state, &no_input);
        CHECK(state.wave == wave);
        CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == expected[wave]);
    }
}

static void test_asteroid_shapes(void) {
    GameState state;
    int index;
    int point;

    game_init(&state, 320, 200);
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        const GameAsteroid *asteroid = &state.asteroids[index];
        if (!asteroid->active) {
            continue;
        }
        CHECK(asteroid->point_count >= 8 && asteroid->point_count <= 11);
        for (point = 0; point < asteroid->point_count; ++point) {
            CHECK(asteroid->radius[point] >= 12 && asteroid->radius[point] <= 19);
        }
    }
}

static void test_bullet_breaks_asteroids_and_scores(void) {
    GameState state;
    const int expected_score[] = {0, 100, 50, 20};
    int size;

    game_init(&state, 320, 200);

    for (size = GAME_ASTEROID_LARGE; size >= GAME_ASTEROID_SMALL; --size) {
        clear_field(&state);
        state.score = 0;
        state.asteroids[1].active = 1;
        state.asteroids[1].size = (uint8_t) size;
        state.asteroids[1].point_count = 8;
        state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
        state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
        state.bullets[0].active = 1;
        state.bullets[0].life = 4;
        state.bullets[0].x = state.asteroids[1].x;
        state.bullets[0].y = state.asteroids[1].y;

        game_step(&state, &no_input);

        CHECK(state.score == (uint32_t) expected_score[size]);
        CHECK(!state.bullets[0].active);
        if (size > GAME_ASTEROID_SMALL) {
            CHECK(count_asteroids(&state, size - 1) == 2 + (size - 1 == GAME_ASTEROID_SMALL ? 1 : 0));
        }
    }
}

static void test_ship_wraps_across_screen(void) {
    GameState state;

    game_init(&state, 320, 200);
    state.ship.x = -1;
    state.ship.y = -1;

    game_step(&state, &no_input);

    CHECK(state.ship.x >= 0);
    CHECK(state.ship.y >= 0);
    CHECK(state.ship.x < ((long) GAME_WORLD_WIDTH << GAME_FIX_SHIFT));
    CHECK(state.ship.y < ((long) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT));
}

static void test_turning_speed(void) {
    GameState state;
    GameInput right = {0, 1, 0, 0, 0, 0};
    GameInput left = {1, 0, 0, 0, 0, 0};
    int frame;

    game_init(&state, 320, 200);
    clear_field(&state);
    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &right);
    }
    /* 100 frames x 626 = 62600 of 65536: 0.955 turn clockwise from "up" */
    CHECK(state.ship.angle == (uint16_t) (49152u + 62600u));

    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &left);
    }
    CHECK(state.ship.angle == 49152u);
}

static void test_thrust_and_drag(void) {
    GameState state;
    GameInput thrust = {0, 0, 1, 0, 0, 0};
    GameInput turn = {1, 0, 0, 0, 0, 0};
    int32_t speed_after_ten = 0;
    int32_t coasting;
    int frame;

    game_init(&state, 320, 200);
    clear_field(&state);
    state.ship.invulnerability = 255;

    for (frame = 0; frame < 300; ++frame) {
        game_step(&state, &thrust);
        if (frame == 9) {
            speed_after_ten = -state.ship.vy;
        }
        CHECK(state.ship.vy <= 0);
    }

    /* pointing up: velocity goes up the screen, almost none sideways */
    CHECK(state.ship.vy < 0);
    CHECK(state.ship.vx > -2000 && state.ship.vx < 2000);
    CHECK(speed_after_ten > 25000 && speed_after_ten < 40000);
    /* top speed is 8 px/frame at 60 Hz, 3.84 world px/frame here */
    CHECK(-state.ship.vy <= 251658L + 300);
    CHECK(-state.ship.vy > 200000L);

    /* coasting slows the ship by about 1.2% per frame (turning keeps manual mode without adding speed) */
    coasting = -state.ship.vy;
    for (frame = 0; frame < 50; ++frame) {
        game_step(&state, &turn);
    }
    CHECK(-state.ship.vy < coasting * 6 / 10);
    CHECK(-state.ship.vy > coasting * 4 / 10);
}

static void test_bullets(void) {
    GameState state;
    GameInput fire = {0, 0, 0, 1, 0, 0};
    int frame;
    int alive_frames = 0;
    int fired = 0;

    game_init(&state, 320, 200);
    clear_field(&state);
    state.ship.invulnerability = 255;

    game_step(&state, &fire);
    CHECK(state.bullets[0].active);
    /* bullet flies up at 7 px/frame @60 Hz = 3.36 world px/frame */
    CHECK(state.bullets[0].vy < -219000L && state.bullets[0].vy > -221500L);
    CHECK(state.bullets[0].vx > -300L && state.bullets[0].vx < 300L);

    for (frame = 0; frame < 120; ++frame) {
        int index;
        int active = 0;
        game_step(&state, &fire);
        for (index = 0; index < GAME_MAX_BULLETS; ++index) {
            active += state.bullets[index].active;
        }
        if (active > fired) {
            fired = active;
        }
        if (state.bullets[0].active) {
            ++alive_frames;
        }
    }
    /* a shot lives 50 frames (60 @60 Hz), and the gun fires every 12 frames */
    CHECK(fired >= 4 && fired <= GAME_MAX_BULLETS);
    CHECK(alive_frames > 0);

    /* a single shot flies for 50 frames: 49 left after the frame it was fired */
    game_init(&state, 320, 200);
    clear_field(&state);
    state.ship.invulnerability = 255;
    game_step(&state, &fire);
    CHECK(state.bullets[0].active);
    CHECK(state.bullets[0].life == 49);
    for (frame = 0; frame < 48; ++frame) {
        game_step(&state, &no_input);
    }
    CHECK(state.bullets[0].active);
    CHECK(state.bullets[0].life == 1);
    game_step(&state, &no_input);
    CHECK(!state.bullets[0].active);
}

static void test_ship_collision(void) {
    GameState state;

    game_init(&state, 320, 200);
    clear_field(&state);
    state.asteroids[1] = state.asteroids[0];
    state.asteroids[1].size = GAME_ASTEROID_LARGE;
    state.asteroids[1].x = state.ship.x;
    state.asteroids[1].y = state.ship.y;

    /* invulnerable after spawning: no damage */
    state.ship.invulnerability = 30;
    game_step(&state, &no_input);
    CHECK(state.lives == 3);

    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.lives == 2);
    CHECK(state.ship.invulnerability == 100);
    CHECK(state.ship.x == ((long) (GAME_WORLD_WIDTH / 2) << GAME_FIX_SHIFT));
}

static void test_wave_clear_advances(void) {
    GameState state;

    game_init(&state, 320, 200);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    game_step(&state, &no_input);

    CHECK(state.wave == 2);
    CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == 8);
    CHECK(state.ship.invulnerability == 100);
}

static void test_asteroid_speeds_scale_with_wave(void) {
    GameState state;
    int32_t total_wave1 = 0;
    int32_t total_wave9 = 0;
    int index;
    int wave;

    for (wave = 1; wave <= 9; wave += 8) {
        int32_t total = 0;
        int rocks = 0;
        int round;

        game_init(&state, 320, 200);
        for (round = 0; round < 6; ++round) {
            memset(state.asteroids, 0, sizeof(state.asteroids));
            state.wave = (uint8_t) (wave - 1);
            state.rng_state += (uint32_t) round * 7919u;
            game_step(&state, &no_input);
            for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
                const GameAsteroid *asteroid = &state.asteroids[index];
                if (asteroid->active) {
                    const long vx = asteroid->vx >> 8;
                    const long vy = asteroid->vy >> 8;
                    /* large rocks: at most 1.5 x 0.24 px/frame, times up to 1.4 for wave 9 = 0.5 px/frame = 129 in 8.8 */
                    CHECK(vx * vx + vy * vy <= 140L * 140L);
                    total += (vx < 0 ? -vx : vx) + (vy < 0 ? -vy : vy);
                    ++rocks;
                }
            }
        }
        if (wave == 1) {
            total_wave1 = total / rocks;
        } else {
            total_wave9 = total / rocks;
        }
    }
    /* +5% per wave: 8 waves later rocks are around 1.4x faster */
    CHECK(total_wave9 > total_wave1 * 12 / 10);
    CHECK(total_wave9 < total_wave1 * 17 / 10);
}

static int line_count;
static int lines_out_of_range;
static int color_counts[16];

static void count_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    ++line_count;
    if (x0 < -60 || x0 > 400 || x1 < -60 || x1 > 400 || y0 < -60 || y0 > 260 || y1 < -60 || y1 > 260) {
        ++lines_out_of_range;
    }
    ++color_counts[color & 15];
}

static void test_render(void) {
    GameState state;
    GameInput thrust = {0, 0, 1, 0, 0, 0};

    game_init(&state, 320, 200);
    state.ship.invulnerability = 0;

    line_count = 0;
    lines_out_of_range = 0;
    memset(color_counts, 0, sizeof(color_counts));
    game_render(&state, NULL, count_line, NULL, NULL);

    /* 4 ship edges, and 8-11 edges for each of the 6 large rocks */
    CHECK(color_counts[GAME_COLOR_SHIP] == 4);
    CHECK(color_counts[GAME_COLOR_ASTEROID_LARGE] >= 6 * 8 && color_counts[GAME_COLOR_ASTEROID_LARGE] <= 6 * 11);
    CHECK(color_counts[GAME_COLOR_FLAME] == 0);
    CHECK(lines_out_of_range == 0);

    game_step(&state, &thrust);
    memset(color_counts, 0, sizeof(color_counts));
    game_render(&state, NULL, count_line, NULL, NULL);
    CHECK(color_counts[GAME_COLOR_FLAME] == 2);

    /* medium resolution doubles the horizontal scale */
    game_set_resolution(&state, 640, 200);
    CHECK(state.x_scale == 512);
    CHECK(state.y_scale == 213);
}

static int ship_min_x;
static int ship_max_x;
static int ship_min_y;
static int ship_max_y;

static void capture_ship(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    if (color != GAME_COLOR_SHIP) {
        return;
    }
    if (x0 < ship_min_x) ship_min_x = x0;
    if (x1 < ship_min_x) ship_min_x = x1;
    if (x0 > ship_max_x) ship_max_x = x0;
    if (x1 > ship_max_x) ship_max_x = x1;
    if (y0 < ship_min_y) ship_min_y = y0;
    if (y1 < ship_min_y) ship_min_y = y1;
    if (y0 > ship_max_y) ship_max_y = y0;
    if (y1 > ship_max_y) ship_max_y = y1;
}

static void measure_ship(GameState *state) {
    ship_min_x = 10000;
    ship_max_x = -10000;
    ship_min_y = 10000;
    ship_max_y = -10000;
    game_render(state, NULL, capture_ship, NULL, NULL);
}

static void test_ship_is_long_and_narrow(void) {
    GameState state;

    /* 320x240 has square pixels, so screen extents equal world extents */
    game_init(&state, 320, 240);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ship.invulnerability = 0;

    state.ship.angle = 49152u;   /* up */
    measure_ship(&state);
    CHECK(ship_max_y - ship_min_y >= 15 && ship_max_y - ship_min_y <= 17);
    CHECK(ship_max_x - ship_min_x >= 7 && ship_max_x - ship_min_x <= 9);

    state.ship.angle = 0;        /* right */
    measure_ship(&state);
    CHECK(ship_max_x - ship_min_x >= 15 && ship_max_x - ship_min_x <= 17);
    CHECK(ship_max_y - ship_min_y >= 7 && ship_max_y - ship_min_y <= 9);

    /* real 320x200 screen: 5/6 vertical scale keeps the 4:3 proportions */
    game_set_resolution(&state, 320, 200);
    state.ship.angle = 49152u;
    measure_ship(&state);
    CHECK(ship_max_y - ship_min_y >= 12 && ship_max_y - ship_min_y <= 15);
}

#define MAX_TRACKED_LINES 400
#define MAX_TRACKED_RECTS 80

static int tracked_lines[MAX_TRACKED_LINES][4];
static int tracked_line_count;
static int tracked_rects[MAX_TRACKED_RECTS][4];
static int tracked_rect_count;

static void track_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) color;
    if (tracked_line_count < MAX_TRACKED_LINES) {
        tracked_lines[tracked_line_count][0] = x0;
        tracked_lines[tracked_line_count][1] = y0;
        tracked_lines[tracked_line_count][2] = x1;
        tracked_lines[tracked_line_count][3] = y1;
        ++tracked_line_count;
    }
}

static void track_rect(void *context, int x0, int y0, int x1, int y1) {
    (void) context;
    if (tracked_rect_count < MAX_TRACKED_RECTS) {
        tracked_rects[tracked_rect_count][0] = x0;
        tracked_rects[tracked_rect_count][1] = y0;
        tracked_rects[tracked_rect_count][2] = x1;
        tracked_rects[tracked_rect_count][3] = y1;
        ++tracked_rect_count;
    }
}

static int point_is_covered(int x, int y) {
    int index;

    for (index = 0; index < tracked_rect_count; ++index) {
        if (x >= tracked_rects[index][0] && x <= tracked_rects[index][2] &&
            y >= tracked_rects[index][1] && y <= tracked_rects[index][3]) {
            return 1;
        }
    }
    return 0;
}

/* Everything drawn must sit inside a rectangle reported as dirty, or erasing would leave ghosts. */
static void check_dirty_coverage(GameState *state) {
    int index;

    tracked_line_count = 0;
    tracked_rect_count = 0;
    game_render(state, NULL, track_line, NULL, track_rect);
    CHECK(tracked_line_count > 0);
    for (index = 0; index < tracked_line_count; ++index) {
        CHECK(point_is_covered(tracked_lines[index][0], tracked_lines[index][1]));
        CHECK(point_is_covered(tracked_lines[index][2], tracked_lines[index][3]));
    }
}

static void test_dirty_rects_cover_everything_drawn(void) {
    static const uint16_t widths[2] = {320, 640};
    int variant;

    for (variant = 0; variant < 2; ++variant) {
        GameState state;
        int frame;

        game_init(&state, widths[variant], 200);
        for (frame = 0; frame < 600; ++frame) {
            GameInput input = {0, 0, 0, 0, 0, 0};
            input.thrust = (uint8_t) ((frame / 40) & 1);
            input.left = (uint8_t) ((frame / 25) & 1);
            input.fire = (uint8_t) ((frame / 7) & 1);
            if (frame % 100 == 99) {
                state.wave = 6;   /* bigger fields */
                memset(state.asteroids, 0, sizeof(state.asteroids));
            }
            game_step(&state, &input);
            if (frame % 3 == 0) {
                check_dirty_coverage(&state);
            }
        }
    }
}

static void test_render_cache_is_reused(void) {
    GameState state;
    const GameAsteroid *asteroid;
    int8_t first_x[GAME_MAX_ASTEROID_POINTS];
    int frame;

    game_init(&state, 320, 200);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    game_step(&state, &no_input);
    state.wave = 1;
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.asteroids[0].active = 1;
    state.asteroids[0].size = GAME_ASTEROID_LARGE;
    state.asteroids[0].point_count = 8;
    for (frame = 0; frame < 8; ++frame) {
        state.asteroids[0].radius[frame] = 16;
    }
    state.asteroids[0].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[0].y = 100L << GAME_FIX_SHIFT;
    state.asteroids[0].angle = 0;
    asteroid = &state.asteroids[0];

    game_render(&state, NULL, count_line, NULL, NULL);
    CHECK(asteroid->cache_valid);
    /* vertex 0 points right, at radius 16 */
    CHECK(asteroid->off_x[0] == 16 && asteroid->off_y[0] == 0);
    memcpy(first_x, asteroid->off_x, sizeof(first_x));

    /* a small turn stays in the same 64-step orientation and reuses the cache */
    state.asteroids[0].angle = 500;
    game_render(&state, NULL, count_line, NULL, NULL);
    CHECK(memcmp(first_x, asteroid->off_x, sizeof(first_x)) == 0);

    /* a bigger turn (>5.6 degrees) rebuilds it */
    state.asteroids[0].angle = 3000;
    game_render(&state, NULL, count_line, NULL, NULL);
    CHECK(asteroid->cache_index == 2);
    CHECK(memcmp(first_x, asteroid->off_x, sizeof(first_x)) != 0);

    /* changing resolution invalidates the cached (screen-space) offsets */
    game_set_resolution(&state, 640, 200);
    CHECK(!asteroid->cache_valid);
    game_render(&state, NULL, count_line, NULL, NULL);
    CHECK(asteroid->off_x[0] == 0 || asteroid->off_x[0] > 16);
}

int main(void) {
    test_initial_wave();
    test_wave_sizes();
    test_asteroid_shapes();
    test_bullet_breaks_asteroids_and_scores();
    test_ship_wraps_across_screen();
    test_turning_speed();
    test_thrust_and_drag();
    test_bullets();
    test_ship_collision();
    test_wave_clear_advances();
    test_asteroid_speeds_scale_with_wave();
    test_render();
    test_ship_is_long_and_narrow();
    test_dirty_rects_cover_everything_drawn();
    test_render_cache_is_reused();

    printf("%d checks, %d failures\n", checks, failures);
#ifdef ATARI_ST_TARGET
    printf("press a key\n");
    getchar();
#endif
    return failures != 0;
}
