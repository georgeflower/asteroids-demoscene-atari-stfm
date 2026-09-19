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
#define HYPERSPACE_SAFE_MARGIN 24            /* clear of every rock by this much (60 of Lovable's 800x600 px) */
#define HYPERSPACE_ATTEMPTS 100
#define MAX_LIVES 9

#define BANNER_FRAMES 75
#define GAME_OVER_FRAMES 150
#define GAME_OVER_SKIP_FRAMES 30
#define PROMPT_BLINK_FRAMES 25
#define REPEAT_FIRST_FRAMES 15
#define REPEAT_NEXT_FRAMES 4
#define INITIALS_LENGTH 3

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

static int pressed(uint8_t now, uint8_t before) {
    return now != 0 && before == 0;
}

static void enter_mode(GameState *state, uint8_t mode) {
    state->mode = mode;
    state->mode_timer = 0;
    state->screen_refresh = 2;   /* both screen buffers need the new screen */
}

static void reset_ship(GameState *state) {
    state->ship.x = ((int32_t) GAME_WORLD_WIDTH / 2) << GAME_FIX_SHIFT;
    state->ship.y = ((int32_t) GAME_WORLD_HEIGHT / 2) << GAME_FIX_SHIFT;
    state->ship.vx = 0;
    state->ship.vy = 0;
    state->ship.angle = 49152u;   /* pointing up */
    state->ship.cooldown = 0;
    state->ship.hyperspace_cooldown = 0;
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
    state->banner_timer = BANNER_FRAMES;
}

static void split_asteroid(GameState *state, const GameAsteroid *asteroid) {
    if (asteroid->size <= GAME_ASTEROID_SMALL) {
        return;
    }

    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y);
    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y);
}

/* Award points, and an extra life for every EXTRA_LIFE_INTERVAL crossed. */
static void add_score(GameState *state, uint32_t points) {
    state->score += points;
    while (state->score >= state->next_extra_life) {
        if (state->lives < MAX_LIVES) {
            ++state->lives;
        }
        state->next_extra_life += GAME_EXTRA_LIFE_INTERVAL;
    }
}

/* Jump to a spot clear of every rock (Lovable's hyperspace), then recharge. */
static void hyperspace(GameState *state) {
    GameShip *ship = &state->ship;
    int32_t x = (int32_t) (GAME_WORLD_WIDTH / 2) << GAME_FIX_SHIFT;
    int32_t y = (int32_t) (GAME_WORLD_HEIGHT / 2) << GAME_FIX_SHIFT;
    int attempt;

    for (attempt = 0; attempt < HYPERSPACE_ATTEMPTS; ++attempt) {
        const int32_t try_x = (int32_t) game_rand_below(state, GAME_WORLD_WIDTH) << GAME_FIX_SHIFT;
        const int32_t try_y = (int32_t) game_rand_below(state, GAME_WORLD_HEIGHT) << GAME_FIX_SHIFT;
        int safe = 1;
        int index;

        for (index = 0; index < GAME_MAX_ASTEROIDS && safe; ++index) {
            const GameAsteroid *asteroid = &state->asteroids[index];
            if (asteroid->active &&
                within_radius(try_x, try_y, asteroid->x, asteroid->y,
                              asteroid_radius_table[asteroid->size] + HYPERSPACE_SAFE_MARGIN)) {
                safe = 0;
            }
        }
        if (safe) {
            x = try_x;
            y = try_y;
            break;
        }
    }

    ship->x = x;
    ship->y = y;
    ship->vx = 0;
    ship->vy = 0;
    ship->hyperspace_cooldown = GAME_HYPERSPACE_RECHARGE_FRAMES;
    ship->invulnerability = SHIP_INVULNERABILITY_FRAMES;
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
    if (ship->hyperspace_cooldown > 0) {
        --ship->hyperspace_cooldown;
    }
    if (input->fire && ship->cooldown == 0) {
        fire_bullet(state);
    }
    if (input->hyperspace && ship->hyperspace_cooldown == 0) {
        hyperspace(state);
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
                add_score(state, asteroid_points_table[exploded.size]);
                split_asteroid(state, &exploded);
                break;
            }
        }
    }
}

static void lose_life(GameState *state) {
    if (state->lives > 0) {
        --state->lives;
    }
    reset_ship(state);
    if (state->lives == 0) {
        enter_mode(state, GAME_MODE_GAME_OVER);
    }
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
            lose_life(state);
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

/* ---- high scores ---- */

/* Position the current score would take in the table, or -1 if it does not qualify. */
static int score_rank(const GameState *state) {
    int index;

    if (state->score < GAME_MIN_SCORE_FOR_INITIALS) {
        return -1;
    }
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        if (state->score > state->high_scores[index].score) {
            return index;
        }
    }
    return -1;
}

static void insert_score(GameState *state, int rank) {
    int index;

    for (index = GAME_HIGH_SCORE_COUNT - 1; index > rank; --index) {
        state->high_scores[index] = state->high_scores[index - 1];
    }
    state->high_scores[rank].score = state->score;
    memcpy(state->high_scores[rank].initials, state->entry, INITIALS_LENGTH);
    state->high_scores[rank].initials[INITIALS_LENGTH] = 0;
    state->scores_changed = 1;
}

void game_scores_pack(const GameState *state, uint8_t *out) {
    int index;

    out[0] = 'A';
    out[1] = 'S';
    out[2] = 'T';
    out[3] = '1';
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        const GameHighScore *entry = &state->high_scores[index];
        uint8_t *slot = out + 4 + index * 8;

        slot[0] = (uint8_t) entry->initials[0];
        slot[1] = (uint8_t) entry->initials[1];
        slot[2] = (uint8_t) entry->initials[2];
        slot[3] = 0;
        slot[4] = (uint8_t) (entry->score >> 24);
        slot[5] = (uint8_t) (entry->score >> 16);
        slot[6] = (uint8_t) (entry->score >> 8);
        slot[7] = (uint8_t) entry->score;
    }
}

int game_scores_unpack(GameState *state, const uint8_t *data) {
    GameHighScore loaded[GAME_HIGH_SCORE_COUNT];
    int index;
    int letter;

    if (data[0] != 'A' || data[1] != 'S' || data[2] != 'T' || data[3] != '1') {
        return 0;
    }
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        const uint8_t *slot = data + 4 + index * 8;

        for (letter = 0; letter < INITIALS_LENGTH; ++letter) {
            if (!((slot[letter] >= 'A' && slot[letter] <= 'Z') || slot[letter] == '-')) {
                return 0;
            }
            loaded[index].initials[letter] = (char) slot[letter];
        }
        loaded[index].initials[INITIALS_LENGTH] = 0;
        loaded[index].score = ((uint32_t) slot[4] << 24) | ((uint32_t) slot[5] << 16) |
                              ((uint32_t) slot[6] << 8) | (uint32_t) slot[7];
    }
    memcpy(state->high_scores, loaded, sizeof(loaded));
    return 1;
}

/* ---- game flow ---- */

void game_start(GameState *state) {
    state->score = 0;
    state->next_extra_life = GAME_EXTRA_LIFE_INTERVAL;
    state->wave = 1;
    state->lives = GAME_START_LIVES;
    state->paused = 0;
    memset(state->asteroids, 0, sizeof(state->asteroids));
    memset(state->bullets, 0, sizeof(state->bullets));
    reset_ship(state);
    state->rng_state ^= (uint32_t) state->frame * 2654435761u;   /* a different field every game */
    spawn_wave(state);
    enter_mode(state, GAME_MODE_PLAYING);
}

static void set_field(GameState *state, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    state->field_x = x;
    state->field_y = y;
    state->field_width = width;
    state->field_height = height;
    state->x_scale = (uint16_t) (((uint32_t) width * 256u) / GAME_WORLD_WIDTH);
    state->y_scale = (uint16_t) (((uint32_t) height * 256u) / GAME_WORLD_HEIGHT);
}

void game_init(GameState *state, uint16_t field_x, uint16_t field_y, uint16_t field_width, uint16_t field_height) {
    int index;

    memset(state, 0, sizeof(*state));
    state->rng_state = 0x1badc0deu;
    state->lives = GAME_START_LIVES;
    state->wave = 1;
    state->next_extra_life = GAME_EXTRA_LIFE_INTERVAL;
    set_field(state, field_x, field_y, field_width, field_height);
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        state->high_scores[index].initials[0] = '-';
        state->high_scores[index].initials[1] = '-';
        state->high_scores[index].initials[2] = '-';
    }
    reset_ship(state);
    state->prompt_visible = 1;
    state->hud_refresh = 2;
    enter_mode(state, GAME_MODE_TITLE);
}

static void step_title(GameState *state, const GameInput *input, const GameInput *previous) {
    if (pressed(input->start, previous->start) || pressed(input->fire, previous->fire)) {
        game_start(state);
        return;
    }
    if (++state->mode_timer >= PROMPT_BLINK_FRAMES) {
        state->mode_timer = 0;
        state->prompt_visible ^= 1;
        state->prompt_refresh = 2;
    }
}

static void step_playing(GameState *state, const GameInput *input, const GameInput *previous) {
    if (pressed(input->pause, previous->pause)) {
        state->paused ^= 1;
    }
    if (state->paused) {
        return;
    }

    update_ship(state, input);
    update_bullets(state);
    update_asteroids(state);
    resolve_bullet_collisions(state);
    resolve_ship_collisions(state);

    if (state->banner_timer > 0) {
        --state->banner_timer;
    }
    if (state->mode == GAME_MODE_PLAYING && !active_asteroids(state)) {
        ++state->wave;
        reset_ship(state);
        spawn_wave(state);
    }
}

static void step_game_over(GameState *state, const GameInput *input, const GameInput *previous) {
    const int skip = state->mode_timer >= GAME_OVER_SKIP_FRAMES &&
                     (pressed(input->start, previous->start) || pressed(input->fire, previous->fire));

    if (++state->mode_timer < GAME_OVER_FRAMES && !skip) {
        return;
    }
    if (score_rank(state) >= 0) {
        state->entry[0] = 'A';
        state->entry[1] = 'A';
        state->entry[2] = 'A';
        state->entry[3] = 0;
        state->entry_position = 0;
        state->repeat_timer = 0;
        enter_mode(state, GAME_MODE_ENTER_INITIALS);
    } else {
        enter_mode(state, GAME_MODE_TITLE);
    }
}

static void step_enter_initials(GameState *state, const GameInput *input, const GameInput *previous) {
    int move = 0;

    if (input->left && !input->right) {
        if (!previous->left) {
            move = -1;
            state->repeat_timer = REPEAT_FIRST_FRAMES;
        } else if (state->repeat_timer > 0 && --state->repeat_timer == 0) {
            move = -1;
            state->repeat_timer = REPEAT_NEXT_FRAMES;
        }
    } else if (input->right && !input->left) {
        if (!previous->right) {
            move = 1;
            state->repeat_timer = REPEAT_FIRST_FRAMES;
        } else if (state->repeat_timer > 0 && --state->repeat_timer == 0) {
            move = 1;
            state->repeat_timer = REPEAT_NEXT_FRAMES;
        }
    }

    if (move != 0) {
        char *letter = &state->entry[state->entry_position];

        *letter = (char) (*letter + move);
        if (*letter > 'Z') {
            *letter = 'A';
        } else if (*letter < 'A') {
            *letter = 'Z';
        }
        state->screen_refresh = 2;
    }

    if (pressed(input->start, previous->start) || pressed(input->fire, previous->fire)) {
        ++state->entry_position;
        if (state->entry_position < INITIALS_LENGTH) {
            /* the next letter starts where this one ended, which is quicker for repeated letters */
            state->entry[state->entry_position] = state->entry[state->entry_position - 1];
            state->screen_refresh = 2;
        } else {
            const int rank = score_rank(state);
            if (rank >= 0) {
                insert_score(state, rank);
            }
            enter_mode(state, GAME_MODE_TITLE);
        }
    }
}

void game_step(GameState *state, const GameInput *input) {
    const GameInput previous = state->previous;

    state->previous = *input;
    ++state->frame;

    switch (state->mode) {
    case GAME_MODE_PLAYING:
        step_playing(state, input, &previous);
        break;
    case GAME_MODE_GAME_OVER:
        step_game_over(state, input, &previous);
        break;
    case GAME_MODE_ENTER_INITIALS:
        step_enter_initials(state, input, &previous);
        break;
    default:
        step_title(state, input, &previous);
        break;
    }
}

/* ---- rendering ---- */

/* World position (16.16) to screen coordinate; keeps 5 fractional bits so slow rocks move smoothly. */
static inline __attribute__((always_inline)) int screen_x(const GameState *state, int32_t world_x) {
    return state->field_x + (int) (mul16((int16_t) (world_x >> 11), (int16_t) state->x_scale) >> 13);
}

static inline __attribute__((always_inline)) int screen_y(const GameState *state, int32_t world_y) {
    return state->field_y + (int) (mul16((int16_t) (world_y >> 11), (int16_t) state->y_scale) >> 13);
}

/* World-space offset (whole pixels) to screen-space offset. */
static inline __attribute__((always_inline)) int scale_x(const GameState *state, int offset) {
    return (int) (mul16((int16_t) offset, (int16_t) state->x_scale) >> 8);
}

static inline __attribute__((always_inline)) int scale_y(const GameState *state, int offset) {
    return (int) (mul16((int16_t) offset, (int16_t) state->y_scale) >> 8);
}

static void mark_rect(const GameRenderer *renderer, int x0, int y0, int x1, int y1) {
    if (renderer->dirty != NULL) {
        renderer->dirty(renderer->context, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    }
}

/* Draw a closed outline through the polygon drawer, or edge by edge with the line drawer. */
static void draw_outline(const GameRenderer *renderer, const int16_t *points, int count, uint8_t color) {
    int index;

    if (renderer->polygon != NULL) {
        renderer->polygon(renderer->context, points, count, color);
        return;
    }
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        renderer->line(renderer->context, points[index * 2], points[index * 2 + 1], points[next * 2],
                       points[next * 2 + 1], color);
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

static void draw_ship(const GameState *state, const GameRenderer *renderer) {
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
    draw_outline(renderer, points, 4, GAME_COLOR_SHIP);

    if (ship->thrusting) {
        const int length = 9 + (state->frame & 3);
        int16_t tip[2];
        int16_t left[2];
        int16_t right[2];

        ship_point(state, -length, 0, cosine, sine, center_x, center_y, &tip[0], &tip[1]);
        ship_point(state, -5, -2, cosine, sine, center_x, center_y, &left[0], &left[1]);
        ship_point(state, -5, 2, cosine, sine, center_x, center_y, &right[0], &right[1]);
        renderer->line(renderer->context, left[0], left[1], tip[0], tip[1], GAME_COLOR_FLAME);
        renderer->line(renderer->context, right[0], right[1], tip[0], tip[1], GAME_COLOR_FLAME);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, tip[0], tip[1]);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, left[0], left[1]);
        grow_bounds(&min_x, &min_y, &max_x, &max_y, right[0], right[1]);
    }

    mark_rect(renderer, min_x, min_y, max_x, max_y);
}

static void draw_bullets(const GameState *state, const GameRenderer *renderer) {
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
        renderer->line(renderer->context, x, y, x + 1, y, GAME_COLOR_SHIP);
        mark_rect(renderer, x, y, x + 1, y);
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

static void draw_asteroid(const GameState *state, GameAsteroid *asteroid, const GameRenderer *renderer) {
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
    draw_outline(renderer, points, count, asteroid_color_table[asteroid->size]);

    mark_rect(renderer, center_x + asteroid->bound_x0, center_y + asteroid->bound_y0,
              center_x + asteroid->bound_x1, center_y + asteroid->bound_y1);
}

/* ---- text screens ---- */

static void format_number(char *out, uint32_t value, int digits) {
    int index;

    out[digits] = 0;
    for (index = digits - 1; index >= 0; --index) {
        out[index] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
}

static void put_text(const GameRenderer *renderer, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    if (renderer->text != NULL) {
        renderer->text(renderer->context, x, y, text, fg, bg, scale);
    }
}

static int centered_x(const GameState *state, const char *text, int scale) {
    const int cell = (scale == 2) ? 16 : 8;
    const int width = (int) strlen(text) * cell;

    return state->field_x + (((state->field_width - width) / 2) / cell) * cell;
}

/* Text centred on the playing field, y measured from the top of the field. */
static void put_centered(const GameState *state, const GameRenderer *renderer, int y, const char *text,
                         uint8_t fg, uint8_t scale) {
    put_text(renderer, centered_x(state, text, scale), state->field_y + y, text, fg, GAME_COLOR_BLACK, scale);
}

/* The same, for text shown over the moving game: also reports its rectangle so it gets erased. */
static void put_centered_overlay(const GameState *state, const GameRenderer *renderer, int y, const char *text,
                                 uint8_t fg) {
    const int x = centered_x(state, text, 1);
    const int top = state->field_y + y;

    put_text(renderer, x, top, text, fg, GAME_COLOR_BLACK, 1);
    mark_rect(renderer, x, top, x + (int) strlen(text) * 8 - 1, top + 7);
}

static void draw_prompt(const GameState *state, const GameRenderer *renderer) {
    put_centered(state, renderer, 124, "PRESS FIRE TO START",
                 state->prompt_visible ? GAME_COLOR_YELLOW : GAME_COLOR_BLACK, 1);
}

static void draw_title_screen(const GameState *state, const GameRenderer *renderer) {
    char line[24];
    int index;

    put_centered(state, renderer, 10, "ASTEROIDS", GAME_COLOR_SHIP, 2);
    put_centered(state, renderer, 34, "A RETRO VECTOR ARCADE GAME", GAME_COLOR_GREY, 1);
    put_centered(state, renderer, 54, "- HALL OF FAME -", GAME_COLOR_YELLOW, 1);
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        line[0] = (char) ('1' + index);
        line[1] = '.';
        line[2] = ' ';
        memcpy(line + 3, state->high_scores[index].initials, INITIALS_LENGTH);
        line[6] = ' ';
        line[7] = ' ';
        format_number(line + 8, state->high_scores[index].score, 6);
        put_centered(state, renderer, 68 + index * 10, line, index == 0 ? GAME_COLOR_YELLOW : GAME_COLOR_WHITE, 1);
    }
    draw_prompt(state, renderer);
    put_centered(state, renderer, 146, "A/D OR ARROWS TURN  W OR UP THRUST", GAME_COLOR_GREY, 1);
    put_centered(state, renderer, 158, "SPACE FIRE  H HYPERSPACE  P PAUSE", GAME_COLOR_GREY, 1);
}

static void draw_game_over_screen(const GameState *state, const GameRenderer *renderer) {
    char line[24];

    put_centered(state, renderer, 44, "GAME OVER", GAME_COLOR_RED, 2);
    memcpy(line, "YOUR SCORE ", 11);
    format_number(line + 11, state->score, 6);
    put_centered(state, renderer, 86, line, GAME_COLOR_WHITE, 1);
    if (score_rank(state) >= 0) {
        put_centered(state, renderer, 110, "NEW HIGH SCORE!", GAME_COLOR_YELLOW, 1);
    }
}

static void draw_initials_screen(const GameState *state, const GameRenderer *renderer) {
    char line[24];
    char letter[2];
    int index;
    const int letters_x = state->field_x + ((state->field_width - INITIALS_LENGTH * 32) / 2 / 16) * 16;

    put_centered(state, renderer, 26, "NEW HIGH SCORE!", GAME_COLOR_YELLOW, 1);
    memcpy(line, "SCORE ", 6);
    format_number(line + 6, state->score, 6);
    put_centered(state, renderer, 46, line, GAME_COLOR_WHITE, 1);
    put_centered(state, renderer, 74, "ENTER YOUR INITIALS", GAME_COLOR_WHITE, 1);

    letter[1] = 0;
    for (index = 0; index < INITIALS_LENGTH; ++index) {
        letter[0] = state->entry[index];
        put_text(renderer, letters_x + index * 32, state->field_y + 96, letter,
                 index == state->entry_position ? GAME_COLOR_YELLOW : GAME_COLOR_WHITE, GAME_COLOR_BLACK, 2);
        put_text(renderer, letters_x + index * 32, state->field_y + 118, index == state->entry_position ? "^" : " ",
                 GAME_COLOR_YELLOW, GAME_COLOR_BLACK, 1);
    }
    put_centered(state, renderer, 146, "LEFT/RIGHT CHANGE  FIRE ACCEPT", GAME_COLOR_GREY, 1);
}

/* ---- HUD (in the frame above the playing field) ---- */

static uint8_t hyperspace_percent(const GameState *state) {
    if (state->ship.hyperspace_cooldown == 0) {
        return 100;
    }
    return (uint8_t) (mul16((int16_t) (GAME_HYPERSPACE_RECHARGE_FRAMES - state->ship.hyperspace_cooldown), 205) >> 10);
}

static void draw_hud(const GameState *state, const GameRenderer *renderer) {
    char text[16];
    char icons[10];
    const uint32_t high = (state->high_scores[0].score > state->score) ? state->high_scores[0].score : state->score;
    int index;
    int count;

    memcpy(text, "SCORE ", 6);
    format_number(text + 6, state->score % 1000000u, 6);
    put_text(renderer, 8, 0, text, GAME_COLOR_YELLOW, GAME_COLOR_FRAME, 1);

    memcpy(text, "HI ", 3);
    format_number(text + 3, high % 1000000u, 6);
    put_text(renderer, 120, 0, text, GAME_COLOR_WHITE, GAME_COLOR_FRAME, 1);

    memcpy(text, "WAVE ", 5);
    format_number(text + 5, state->wave % 100u, 2);
    put_text(renderer, 240, 0, text, GAME_COLOR_WHITE, GAME_COLOR_FRAME, 1);

    put_text(renderer, 8, 8, "LIVES ", GAME_COLOR_WHITE, GAME_COLOR_FRAME, 1);
    memset(icons, ' ', 8);
    icons[8] = 0;
    if (state->lives <= 6) {
        for (index = 0; index < state->lives; ++index) {
            icons[index] = 127;
        }
    } else {
        icons[0] = 127;
        icons[1] = 'X';
        icons[2] = (char) ('0' + state->lives);
    }
    put_text(renderer, 56, 8, icons, GAME_COLOR_SHIP, GAME_COLOR_FRAME, 1);

    count = hyperspace_percent(state);
    if (count >= 100) {
        put_text(renderer, 120, 8, "HYPER READY ", GAME_COLOR_SHIP, GAME_COLOR_FRAME, 1);
    } else {
        memcpy(text, "HYPER ", 6);
        format_number(text + 6, (uint32_t) count, 2);
        text[8] = '%';
        text[9] = ' ';
        text[10] = ' ';
        text[11] = 0;
        put_text(renderer, 120, 8, text, GAME_COLOR_GREY, GAME_COLOR_FRAME, 1);
    }
}

static void update_hud(GameState *state, const GameRenderer *renderer) {
    const uint32_t high = state->high_scores[0].score;
    const uint8_t percent = hyperspace_percent(state);

    if (state->hud_score != state->score || state->hud_high != high || state->hud_lives != state->lives ||
        state->hud_wave != state->wave || state->hud_hyperspace != percent) {
        state->hud_score = state->score;
        state->hud_high = high;
        state->hud_lives = state->lives;
        state->hud_wave = state->wave;
        state->hud_hyperspace = percent;
        state->hud_refresh = 2;
    }
    if (state->hud_refresh > 0) {
        draw_hud(state, renderer);
        --state->hud_refresh;
    }
}

void game_render(GameState *state, const GameRenderer *renderer) {
    int index;

    if (state->screen_refresh > 0 && renderer->clear_field != NULL) {
        renderer->clear_field(renderer->context);
    }

    if (state->mode == GAME_MODE_PLAYING) {
        draw_ship(state, renderer);
        draw_bullets(state, renderer);
        for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
            if (state->asteroids[index].active) {
                draw_asteroid(state, &state->asteroids[index], renderer);
            }
        }

        if (state->paused) {
            put_centered_overlay(state, renderer, 84, "PAUSED", GAME_COLOR_YELLOW);
        } else if (state->banner_timer > 0) {
            char banner[8];

            memcpy(banner, "WAVE ", 5);
            format_number(banner + 5, state->wave % 100u, 2);
            put_centered_overlay(state, renderer, 60, banner, GAME_COLOR_WHITE);
        }
    } else if (state->screen_refresh > 0) {
        if (state->mode == GAME_MODE_TITLE) {
            draw_title_screen(state, renderer);
        } else if (state->mode == GAME_MODE_GAME_OVER) {
            draw_game_over_screen(state, renderer);
        } else {
            draw_initials_screen(state, renderer);
        }
        state->prompt_refresh = 0;
    } else if (state->mode == GAME_MODE_TITLE && state->prompt_refresh > 0) {
        draw_prompt(state, renderer);
        --state->prompt_refresh;
    }

    if (state->screen_refresh > 0) {
        --state->screen_refresh;
    }
    update_hud(state, renderer);
}
