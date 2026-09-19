#include "game.h"

#include "trig_table.h"

#include <stddef.h>
#include <string.h>

/*
 * Constants are the Lovable version's values (800x600 canvas, 60 Hz) converted
 * to the 320x240 world and 50 Hz frames: distances x0.4, speeds x0.4x1.2,
 * accelerations x0.4x1.44, durations x(50/60).  16.16 fixed point.
 */
#define SHIP_TURN_STEP 626                   /* 0.05 rad/frame @60 Hz, in 1/65536 turn @50 Hz */
#define SHIP_THRUST 3020                     /* 0.08 px/frame^2 */
#define SHIP_MAX_SPEED 251658L               /* 8 px/frame */
#define SHIP_DRAG_NUMERATOR 3                /* v -= v/256*3 -> x0.988 per frame (0.99 @60 Hz) */
#define SHIP_RADIUS 6
#define SHIP_COOLDOWN_FRAMES 12              /* 250 ms */
#define SHIP_INVULNERABILITY_FRAMES 100      /* 120 frames @60 Hz = 2 s */
#define SHIP_BLINK_FRAMES 5                  /* 100 ms */
#define BULLET_SPEED 220201L                 /* 7 px/frame */
#define BULLET_LIFE_FRAMES 50                /* 60 frames @60 Hz = 1 s */
#define BULLET_RADIUS 1
#define WAVE_BASE_ASTEROIDS 4
#define WAVE_MAX_ASTEROIDS 20
#define WAVE_SPAWN_MIN_DISTANCE 60           /* from the centre, so the ship starts safe */
#define SPEED_SCALE_STEP 13                  /* +5% per wave, 8.8 */
#define SPEED_SCALE_MAX_WAVE 40
#define NUDGE_SIN 2286                       /* sin(0.14 rad), Q14 */
#define NUDGE_COS 16219                      /* cos(0.14 rad), Q14 */

static const uint8_t asteroid_radius_table[4] = {0, 5, 10, 16};
static const int32_t asteroid_speed_table[4] = {0, 37749, 25166, 15729};
static const uint16_t asteroid_points_table[4] = {0, 100, 50, 20};
static const uint8_t asteroid_color_table[4] = {
    0, GAME_COLOR_ASTEROID_SMALL, GAME_COLOR_ASTEROID_MEDIUM, GAME_COLOR_ASTEROID_LARGE
};
static const uint16_t asteroid_angle_step[4] = {8192, 7282, 6553, 5957};   /* 65536 / (8..11) */

static const int8_t ship_shape[4][2] = {{8, 0}, {-8, -4}, {-4, 0}, {-8, 4}};

/* 16x16 -> 32 bit multiply: a single muls.w on the 68000 instead of a library call. */
static inline __attribute__((always_inline)) int32_t mul16(int16_t a, int16_t b) {
    return (int32_t) a * b;
}

static inline __attribute__((always_inline)) int32_t trig_sin(uint16_t angle) {
    return game_sin_table[(angle >> 8) & 255u];
}

static inline __attribute__((always_inline)) int32_t trig_cos(uint16_t angle) {
    return game_sin_table[((angle >> 8) + 64u) & 255u];
}

/* magnitude (16.16) * trig (Q14) */
static inline __attribute__((always_inline)) int32_t trig_mul(int32_t magnitude, int32_t trig) {
    return mul16((int16_t) (magnitude >> 4), (int16_t) trig) >> 10;
}

static uint32_t isqrt32(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1ul << 30;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint16_t game_rand16(GameState *state) {
    state->rng_state = state->rng_state * 1103515245u + 12345u;
    return (uint16_t) (state->rng_state >> 16);
}

static int game_rand_below(GameState *state, int limit) {
    return (int) (((uint32_t) game_rand16(state) * (uint32_t) limit) >> 16);
}

static int wrap_world(int32_t *x, int32_t *y) {
    const int32_t max_x = (int32_t) GAME_WORLD_WIDTH << GAME_FIX_SHIFT;
    const int32_t max_y = (int32_t) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT;
    int wrapped = 0;

    while (*x < 0) {
        *x += max_x;
        wrapped = 1;
    }
    while (*x >= max_x) {
        *x -= max_x;
        wrapped = 1;
    }
    while (*y < 0) {
        *y += max_y;
        wrapped = 1;
    }
    while (*y >= max_y) {
        *y -= max_y;
        wrapped = 1;
    }
    return wrapped;
}

/* Circle test on 12.4 coordinates; radius in world pixels. */
static int within_radius(int32_t ax, int32_t ay, int32_t bx, int32_t by, int radius) {
    const int16_t dx = (int16_t) ((ax - bx) >> 12);
    const int16_t dy = (int16_t) ((ay - by) >> 12);
    const int16_t r = (int16_t) (radius << 4);

    if (dx > r || dx < -r || dy > r || dy < -r) {
        return 0;
    }
    return (mul16(dx, dx) + mul16(dy, dy)) < mul16(r, r);
}

static void reset_ship(GameState *state) {
    state->ship.x = ((int32_t) GAME_WORLD_WIDTH / 2) << GAME_FIX_SHIFT;
    state->ship.y = ((int32_t) GAME_WORLD_HEIGHT / 2) << GAME_FIX_SHIFT;
    state->ship.vx = 0;
    state->ship.vy = 0;
    state->ship.angle = 49152u;   /* pointing up */
    state->ship.cooldown = 0;
    state->ship.invulnerability = SHIP_INVULNERABILITY_FRAMES;
    state->ship.thrusting = 0;
}

static int find_free_asteroid(GameState *state) {
    int index;
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (!state->asteroids[index].active) {
            return index;
        }
    }
    return -1;
}

static void spawn_asteroid(GameState *state, uint8_t size, int32_t x, int32_t y) {
    GameAsteroid *asteroid;
    const int slot = find_free_asteroid(state);
    const int base_radius = asteroid_radius_table[size];
    const int wave = (state->wave > SPEED_SCALE_MAX_WAVE) ? SPEED_SCALE_MAX_WAVE : state->wave;
    int32_t variance;
    int32_t speed;
    uint16_t heading;
    int index;

    if (slot < 0) {
        return;
    }

    asteroid = &state->asteroids[slot];
    memset(asteroid, 0, sizeof(*asteroid));
    asteroid->active = 1;
    asteroid->size = size;
    asteroid->x = x;
    asteroid->y = y;
    asteroid->point_count = (uint8_t) (8 + game_rand_below(state, 4));
    for (index = 0; index < asteroid->point_count; ++index) {
        /* radius +-20% */
        asteroid->radius[index] = (uint8_t) ((base_radius * (205 + game_rand_below(state, 103))) >> 8);
    }
    asteroid->angle = game_rand16(state);
    asteroid->spin = (int16_t) (game_rand_below(state, 501) - 250);

    /* large rocks vary 0.3..1.5x, the others 0.8..1.2x */
    variance = (size == GAME_ASTEROID_LARGE) ? 77 + game_rand_below(state, 308) : 205 + game_rand_below(state, 103);
    speed = (asteroid_speed_table[size] >> 8) * variance;
    speed = (speed * (256 + SPEED_SCALE_STEP * (wave - 1))) >> 8;
    heading = game_rand16(state);
    asteroid->vx = trig_mul(speed, trig_cos(heading));
    asteroid->vy = trig_mul(speed, trig_sin(heading));
}

static void spawn_wave(GameState *state) {
    int count = WAVE_BASE_ASTEROIDS + 2 * state->wave;
    int index;

    if (count > WAVE_MAX_ASTEROIDS) {
        count = WAVE_MAX_ASTEROIDS;
    }

    for (index = 0; index < count; ++index) {
        int retries;
        for (retries = 0; retries < 100; ++retries) {
            const int x = game_rand_below(state, GAME_WORLD_WIDTH);
            const int y = game_rand_below(state, GAME_WORLD_HEIGHT);
            const int dx = x - GAME_WORLD_WIDTH / 2;
            const int dy = y - GAME_WORLD_HEIGHT / 2;
            if (dx * dx + dy * dy > WAVE_SPAWN_MIN_DISTANCE * WAVE_SPAWN_MIN_DISTANCE) {
                spawn_asteroid(state, GAME_ASTEROID_LARGE, (int32_t) x << GAME_FIX_SHIFT, (int32_t) y << GAME_FIX_SHIFT);
                break;
            }
        }
    }
}

static void split_asteroid(GameState *state, const GameAsteroid *asteroid) {
    if (asteroid->size <= GAME_ASTEROID_SMALL) {
        return;
    }

    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y);
    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y);
}

static uint8_t any_manual_input(const GameInput *input) {
    return (uint8_t) (input->left || input->right || input->thrust || input->fire);
}

static void select_demo_input(const GameState *state, GameInput *ai) {
    const GameAsteroid *target = NULL;
    int32_t best_distance = 0x7fffffffl;
    int index;

    memset(ai, 0, sizeof(*ai));

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        const GameAsteroid *asteroid = &state->asteroids[index];
        if (asteroid->active) {
            const int32_t dx = (asteroid->x - state->ship.x) >> GAME_FIX_SHIFT;
            const int32_t dy = (asteroid->y - state->ship.y) >> GAME_FIX_SHIFT;
            const int32_t distance = mul16((int16_t) dx, (int16_t) dx) + mul16((int16_t) dy, (int16_t) dy);
            if (distance < best_distance) {
                best_distance = distance;
                target = asteroid;
            }
        }
    }

    if (target != NULL) {
        const int32_t dx = (target->x - state->ship.x) >> GAME_FIX_SHIFT;
        const int32_t dy = (target->y - state->ship.y) >> GAME_FIX_SHIFT;
        int32_t best_dot = -0x7fffffffl;
        uint16_t best_angle = 0;
        int16_t difference;
        int step;

        for (step = 0; step < 32; ++step) {
            const uint16_t angle = (uint16_t) (step << 11);
            const int32_t dot = mul16((int16_t) trig_cos(angle), (int16_t) dx) + mul16((int16_t) trig_sin(angle), (int16_t) dy);
            if (dot > best_dot) {
                best_dot = dot;
                best_angle = angle;
            }
        }

        difference = (int16_t) (uint16_t) (best_angle - state->ship.angle);
        if (difference > SHIP_TURN_STEP) {
            ai->right = 1;
        } else if (difference < -SHIP_TURN_STEP) {
            ai->left = 1;
        }

        if (best_distance > 60 * 60) {
            ai->thrust = 1;
        }
        if (!ai->left && !ai->right) {
            ai->fire = 1;
        }
    }
}

static void fire_bullet(GameState *state) {
    int index;
    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        GameBullet *bullet = &state->bullets[index];
        if (!bullet->active) {
            bullet->active = 1;
            bullet->life = BULLET_LIFE_FRAMES;
            bullet->x = state->ship.x;
            bullet->y = state->ship.y;
            bullet->vx = state->ship.vx + trig_mul(BULLET_SPEED, trig_cos(state->ship.angle));
            bullet->vy = state->ship.vy + trig_mul(BULLET_SPEED, trig_sin(state->ship.angle));
            state->ship.cooldown = SHIP_COOLDOWN_FRAMES;
            return;
        }
    }
}

static void update_ship(GameState *state, const GameInput *input) {
    GameShip *ship = &state->ship;
    const int32_t max_speed = SHIP_MAX_SPEED >> 8;
    int32_t vx8;
    int32_t vy8;
    int32_t speed;

    if (input->left) {
        ship->angle = (uint16_t) (ship->angle - SHIP_TURN_STEP);
    }
    if (input->right) {
        ship->angle = (uint16_t) (ship->angle + SHIP_TURN_STEP);
    }

    ship->thrusting = (uint8_t) (input->thrust != 0);
    if (ship->thrusting) {
        ship->vx += trig_mul(SHIP_THRUST, trig_cos(ship->angle));
        ship->vy += trig_mul(SHIP_THRUST, trig_sin(ship->angle));
    }

    if (ship->cooldown > 0) {
        --ship->cooldown;
    }
    if (ship->invulnerability > 0) {
        --ship->invulnerability;
    }
    if (input->fire && ship->cooldown == 0) {
        fire_bullet(state);
    }

    vx8 = ship->vx >> 8;
    vy8 = ship->vy >> 8;
    speed = (int32_t) isqrt32((uint32_t) ((vx8 * vx8) + (vy8 * vy8)));
    if (speed > max_speed) {
        ship->vx = (ship->vx * max_speed) / speed;
        ship->vy = (ship->vy * max_speed) / speed;
    }

    ship->vx -= (ship->vx / 256) * SHIP_DRAG_NUMERATOR;
    ship->vy -= (ship->vy / 256) * SHIP_DRAG_NUMERATOR;
    ship->x += ship->vx;
    ship->y += ship->vy;
    wrap_world(&ship->x, &ship->y);
}

static void update_bullets(GameState *state) {
    int index;
    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        GameBullet *bullet = &state->bullets[index];
        if (!bullet->active) {
            continue;
        }
        bullet->x += bullet->vx;
        bullet->y += bullet->vy;
        wrap_world(&bullet->x, &bullet->y);
        if (bullet->life > 0) {
            --bullet->life;
        }
        if (bullet->life == 0) {
            bullet->active = 0;
        }
    }
}

/* Rotate an asteroid's heading towards the ship (at most 0.14 rad) after it wraps around the screen. */
static void nudge_towards_ship(const GameState *state, GameAsteroid *asteroid) {
    const int32_t vx8 = asteroid->vx >> 8;
    const int32_t vy8 = asteroid->vy >> 8;
    const int32_t dx = (state->ship.x - asteroid->x) >> GAME_FIX_SHIFT;
    const int32_t dy = (state->ship.y - asteroid->y) >> GAME_FIX_SHIFT;
    const int32_t cross = (vx8 * dy) - (vy8 * dx);
    const int32_t dot = (vx8 * dx) + (vy8 * dy);
    const int32_t speed = (int32_t) isqrt32((uint32_t) ((vx8 * vx8) + (vy8 * vy8)));
    const int32_t distance = (int32_t) isqrt32((uint32_t) ((dx * dx) + (dy * dy)));
    int32_t sine;
    int32_t cosine;
    int32_t vx;
    int32_t vy;

    if (cross == 0 || speed == 0 || distance == 0) {
        return;
    }

    if (dot > 0 && ((cross < 0 ? -cross : cross) < ((speed * distance * 36) >> 8))) {
        /* already within 0.14 rad of the ship: turn exactly onto it (small-angle approximation) */
        sine = (cross * 16384) / (speed * distance);
        cosine = 16384 - ((sine * sine) >> 15);
    } else {
        sine = (cross > 0) ? NUDGE_SIN : -NUDGE_SIN;
        cosine = NUDGE_COS;
    }

    vx = asteroid->vx >> 4;
    vy = asteroid->vy >> 4;
    asteroid->vx = ((vx * cosine) - (vy * sine)) >> 10;
    asteroid->vy = ((vx * sine) + (vy * cosine)) >> 10;
}

static void update_asteroids(GameState *state) {
    int index;
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        GameAsteroid *asteroid = &state->asteroids[index];
        if (!asteroid->active) {
            continue;
        }
        asteroid->x += asteroid->vx;
        asteroid->y += asteroid->vy;
        asteroid->angle = (uint16_t) (asteroid->angle + (uint16_t) asteroid->spin);
        if (wrap_world(&asteroid->x, &asteroid->y)) {
            nudge_towards_ship(state, asteroid);
        }
    }
}

static void resolve_bullet_collisions(GameState *state) {
    int bullet_index;
    int asteroid_index;

    for (bullet_index = 0; bullet_index < GAME_MAX_BULLETS; ++bullet_index) {
        GameBullet *bullet = &state->bullets[bullet_index];
        if (!bullet->active) {
            continue;
        }
        for (asteroid_index = 0; asteroid_index < GAME_MAX_ASTEROIDS; ++asteroid_index) {
            GameAsteroid *asteroid = &state->asteroids[asteroid_index];
            if (!asteroid->active) {
                continue;
            }
            if (within_radius(bullet->x, bullet->y, asteroid->x, asteroid->y,
                              asteroid_radius_table[asteroid->size] + BULLET_RADIUS)) {
                GameAsteroid exploded = *asteroid;
                bullet->active = 0;
                asteroid->active = 0;
                state->score += asteroid_points_table[exploded.size];
                split_asteroid(state, &exploded);
                break;
            }
        }
    }
}

/* Fresh game: score, lives, wave and playfield reset (the random generator carries on). */
static void start_new_game(GameState *state) {
    state->score = 0;
    state->wave = 1;
    state->lives = 3;
    memset(state->asteroids, 0, sizeof(state->asteroids));
    memset(state->bullets, 0, sizeof(state->bullets));
    reset_ship(state);
    spawn_wave(state);
}

static void resolve_ship_collisions(GameState *state) {
    int asteroid_index;

    if (state->ship.invulnerability > 0) {
        return;
    }

    for (asteroid_index = 0; asteroid_index < GAME_MAX_ASTEROIDS; ++asteroid_index) {
        GameAsteroid *asteroid = &state->asteroids[asteroid_index];
        if (!asteroid->active) {
            continue;
        }
        if (within_radius(state->ship.x, state->ship.y, asteroid->x, asteroid->y,
                          asteroid_radius_table[asteroid->size] + SHIP_RADIUS)) {
            if (state->lives > 0) {
                --state->lives;
            }
            reset_ship(state);
            if (state->lives == 0) {
                /* game over: back to the attract screen, where the autopilot plays until a key is pressed */
                start_new_game(state);
                state->demo_mode = 1;
            }
            break;
        }
    }
}

static uint8_t active_asteroids(const GameState *state) {
    int index;
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state->asteroids[index].active) {
            return 1;
        }
    }
    return 0;
}

void game_init(GameState *state, uint16_t width, uint16_t height) {
    memset(state, 0, sizeof(*state));
    state->rng_state = 0x1badc0deu;
    state->lives = 3;
    state->wave = 1;
    state->demo_mode = 1;
    game_set_resolution(state, width, height);
    reset_ship(state);
    spawn_wave(state);
}

void game_set_resolution(GameState *state, uint16_t width, uint16_t height) {
    int index;

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        state->asteroids[index].cache_valid = 0;
    }
    state->width = width;
    state->height = height;
    state->x_scale = (uint16_t) (((uint32_t) width * 256u) / GAME_WORLD_WIDTH);
    state->y_scale = (uint16_t) (((uint32_t) height * 256u) / GAME_WORLD_HEIGHT);
}

void game_step(GameState *state, const GameInput *input) {
    GameInput effective_input;
    ++state->frame;

    if (state->demo_mode && any_manual_input(input)) {
        /* the first key press ends the attract demo and starts a real game */
        start_new_game(state);
        state->demo_mode = 0;
    }

    if (state->demo_mode) {
        select_demo_input(state, &effective_input);
    } else {
        effective_input = *input;
    }

    update_ship(state, &effective_input);
    update_bullets(state);
    update_asteroids(state);
    resolve_bullet_collisions(state);
    resolve_ship_collisions(state);

    if (!active_asteroids(state)) {
        ++state->wave;
        reset_ship(state);
        spawn_wave(state);
    }
}


/* World position (16.16) to screen coordinate; keeps 5 fractional bits so slow rocks move smoothly. */
static inline __attribute__((always_inline)) int screen_x(const GameState *state, int32_t world_x) {
    return (int) (mul16((int16_t) (world_x >> 11), (int16_t) state->x_scale) >> 13);
}

static inline __attribute__((always_inline)) int screen_y(const GameState *state, int32_t world_y) {
    return (int) (mul16((int16_t) (world_y >> 11), (int16_t) state->y_scale) >> 13);
}

/* World-space offset (whole pixels) to screen-space offset. */
static inline __attribute__((always_inline)) int scale_x(const GameState *state, int offset) {
    return (int) (mul16((int16_t) offset, (int16_t) state->x_scale) >> 8);
}

static inline __attribute__((always_inline)) int scale_y(const GameState *state, int offset) {
    return (int) (mul16((int16_t) offset, (int16_t) state->y_scale) >> 8);
}

static void mark_rect(void *context, GameDirtyMarker mark_dirty, int x0, int y0, int x1, int y1) {
    if (mark_dirty != NULL) {
        mark_dirty(context, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    }
}

/* Draw a closed outline through the polygon drawer, or edge by edge with the line drawer. */
static void draw_outline(void *context, GameLineDrawer draw_line, GamePolygonDrawer draw_polygon,
                         const int16_t *points, int count, uint8_t color) {
    int index;

    if (draw_polygon != NULL) {
        draw_polygon(context, points, count, color);
        return;
    }
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        draw_line(context, points[index * 2], points[index * 2 + 1], points[next * 2], points[next * 2 + 1], color);
    }
}

/* Screen position of a point given in the ship's local frame (x along the heading). */
static void ship_point(const GameState *state, int local_x, int local_y, int32_t cosine, int32_t sine,
                       int center_x, int center_y, int16_t *screen_px, int16_t *screen_py) {
    const int world_x = (int) ((mul16((int16_t) local_x, (int16_t) cosine) - mul16((int16_t) local_y, (int16_t) sine) + 8192) >> 14);
    const int world_y = (int) ((mul16((int16_t) local_x, (int16_t) sine) + mul16((int16_t) local_y, (int16_t) cosine) + 8192) >> 14);

    *screen_px = (int16_t) (center_x + scale_x(state, world_x));
    *screen_py = (int16_t) (center_y + scale_y(state, world_y));
}

static void grow_bounds(int *min_x, int *min_y, int *max_x, int *max_y, int x, int y) {
    if (x < *min_x) {
        *min_x = x;
    }
    if (x > *max_x) {
        *max_x = x;
    }
    if (y < *min_y) {
        *min_y = y;
    }
    if (y > *max_y) {
        *max_y = y;
    }
}

static void draw_ship(const GameState *state, void *context, GameLineDrawer draw_line,
                      GamePolygonDrawer draw_polygon, GameDirtyMarker mark_dirty) {
    const GameShip *ship = &state->ship;
    const int center_x = screen_x(state, ship->x);
    const int center_y = screen_y(state, ship->y);
    const int32_t cosine = trig_cos(ship->angle);
    const int32_t sine = trig_sin(ship->angle);
    int16_t points[8];
    int min_x = center_x;
    int min_y = center_y;
    int max_x = center_x;
    int max_y = center_y;
    int index;

    if (ship->invulnerability > 0 && ((state->frame / SHIP_BLINK_FRAMES) & 1) != 0) {
        return;
    }

    for (index = 0; index < 4; ++index) {
        ship_point(state, ship_shape[index][0], ship_shape[index][1], cosine, sine, center_x, center_y,
                   &points[index * 2], &points[index * 2 + 1]);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, points[index * 2], points[index * 2 + 1]);
    }
    draw_outline(context, draw_line, draw_polygon, points, 4, GAME_COLOR_SHIP);

    if (ship->thrusting) {
        const int length = 9 + (state->frame & 3);
        int16_t tip[2];
        int16_t left[2];
        int16_t right[2];

        ship_point(state, -length, 0, cosine, sine, center_x, center_y, &tip[0], &tip[1]);
        ship_point(state, -5, -2, cosine, sine, center_x, center_y, &left[0], &left[1]);
        ship_point(state, -5, 2, cosine, sine, center_x, center_y, &right[0], &right[1]);
        draw_line(context, left[0], left[1], tip[0], tip[1], GAME_COLOR_FLAME);
        draw_line(context, right[0], right[1], tip[0], tip[1], GAME_COLOR_FLAME);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, tip[0], tip[1]);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, left[0], left[1]);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, right[0], right[1]);
    }

    mark_rect(context, mark_dirty, min_x, min_y, max_x, max_y);
}

static void draw_bullets(const GameState *state, void *context, GameLineDrawer draw_line, GameDirtyMarker mark_dirty) {
    int index;
    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        const GameBullet *bullet = &state->bullets[index];
        int x;
        int y;

        if (!bullet->active) {
            continue;
        }
        x = screen_x(state, bullet->x);
        y = screen_y(state, bullet->y);
        draw_line(context, x, y, x + 1, y, GAME_COLOR_SHIP);
        mark_rect(context, mark_dirty, x, y, x + 1, y);
    }
}

/* Rocks turn in 64 steps (5.6 degrees), so the rotated vertex offsets are cached and reused for several frames. */
static void rebuild_asteroid_cache(const GameState *state, GameAsteroid *asteroid, uint8_t orientation) {
    const uint16_t step = asteroid_angle_step[asteroid->point_count - 8];
    uint16_t angle = (uint16_t) ((uint16_t) orientation << 10);
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    int vertex;

    for (vertex = 0; vertex < asteroid->point_count; ++vertex) {
        const int16_t radius = asteroid->radius[vertex];
        const int world_x = (int) ((mul16(radius, (int16_t) trig_cos(angle)) + 8192) >> 14);
        const int world_y = (int) ((mul16(radius, (int16_t) trig_sin(angle)) + 8192) >> 14);
        const int offset_x = scale_x(state, world_x);
        const int offset_y = scale_y(state, world_y);

        asteroid->off_x[vertex] = (int8_t) offset_x;
        asteroid->off_y[vertex] = (int8_t) offset_y;
        grow_bounds(&min_x, &min_y, &max_x, &max_y, offset_x, offset_y);
        angle = (uint16_t) (angle + step);
    }
    asteroid->bound_x0 = (int8_t) min_x;
    asteroid->bound_y0 = (int8_t) min_y;
    asteroid->bound_x1 = (int8_t) max_x;
    asteroid->bound_y1 = (int8_t) max_y;
    asteroid->cache_index = orientation;
    asteroid->cache_valid = 1;
}

static void draw_asteroid(const GameState *state, GameAsteroid *asteroid, void *context, GameLineDrawer draw_line,
                          GamePolygonDrawer draw_polygon, GameDirtyMarker mark_dirty) {
    const int center_x = screen_x(state, asteroid->x);
    const int center_y = screen_y(state, asteroid->y);
    const int count = asteroid->point_count;
    const uint8_t orientation = (uint8_t) (asteroid->angle >> 10);
    int16_t points[GAME_MAX_ASTEROID_POINTS * 2];
    int vertex;

    if (!asteroid->cache_valid || asteroid->cache_index != orientation) {
        rebuild_asteroid_cache(state, asteroid, orientation);
    }

    for (vertex = 0; vertex < count; ++vertex) {
        points[vertex * 2] = (int16_t) (center_x + asteroid->off_x[vertex]);
        points[vertex * 2 + 1] = (int16_t) (center_y + asteroid->off_y[vertex]);
    }
    draw_outline(context, draw_line, draw_polygon, points, count, asteroid_color_table[asteroid->size]);

    mark_rect(context, mark_dirty, center_x + asteroid->bound_x0, center_y + asteroid->bound_y0,
              center_x + asteroid->bound_x1, center_y + asteroid->bound_y1);
}

void game_render(GameState *state, void *context, GameLineDrawer draw_line, GamePolygonDrawer draw_polygon,
                 GameDirtyMarker mark_dirty) {
    int index;

    draw_ship(state, context, draw_line, draw_polygon, mark_dirty);
    draw_bullets(state, context, draw_line, mark_dirty);

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state->asteroids[index].active) {
            draw_asteroid(state, &state->asteroids[index], context, draw_line, draw_polygon, mark_dirty);
        }
    }
}
