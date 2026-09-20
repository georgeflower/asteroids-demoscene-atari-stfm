#include "game.h"

#include "trig_table.h"

#include <stddef.h>
#include <string.h>

#define ENEMY_UFO_LARGE GAME_ENEMY_UFO_LARGE
#define ENEMY_UFO_SMALL GAME_ENEMY_UFO_SMALL
#define ENEMY_BROWN GAME_ENEMY_BROWN
#define ENEMY_GREEN GAME_ENEMY_GREEN
#define ENEMY_BLUE GAME_ENEMY_BLUE
#define ENEMY_PURPLE GAME_ENEMY_PURPLE
#define BOSS_AMIGA_BALL GAME_BOSS_AMIGA_BALL
#define BOSS_FLYING_SAUCER GAME_BOSS_FLYING_SAUCER
#define BOSS_TENTACLE GAME_BOSS_TENTACLE
#define BOSS_BORG_CUBE GAME_BOSS_BORG_CUBE

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
#define POWERUP_LIFE_FRAMES 250              /* 300 frames @60 Hz */
#define POWERUP_DROP_PERCENT 15
#define POWERUP_RADIUS 6
#define SHIELD_FRAMES 250                    /* 300 @60 Hz */
#define RAPID_FIRE_FRAMES 500                /* 600 @60 Hz */
#define MULTIPLIER_FRAMES 750                /* 900 @60 Hz */
#define RAPID_FIRE_COOLDOWN 5                /* 100 ms */
#define STAR_LAYERS 3

#define BANNER_FRAMES 75
#define GAME_OVER_FRAMES 150
#define GAME_OVER_SKIP_FRAMES 30
#define PROMPT_BLINK_FRAMES 25
#define REPEAT_FIRST_FRAMES 15
#define REPEAT_NEXT_FRAMES 4
#define INITIALS_LENGTH 3

#define ROCK_RADIUS_SMALL 5
#define ROCK_RADIUS_MEDIUM 10
#define ROCK_RADIUS_LARGE 16
static const uint8_t asteroid_radius_table[4] = {0, ROCK_RADIUS_SMALL, ROCK_RADIUS_MEDIUM, ROCK_RADIUS_LARGE};
/* how far (in 12.4 world pixels) a bullet's centre may be from a rock's for a hit: radius plus the bullet's */
static const uint16_t rock_hit_reach[4] = {0, (ROCK_RADIUS_SMALL + 1) << 4, (ROCK_RADIUS_MEDIUM + 1) << 4,
                                           (ROCK_RADIUS_LARGE + 1) << 4};
static const int32_t asteroid_speed_table[4] = {0, 37749, 25166, 15729};
static const uint16_t asteroid_points_table[4] = {0, 100, 50, 20};
static const uint8_t asteroid_color_table[4] = {
    0, GAME_COLOR_ASTEROID_SMALL, GAME_COLOR_ASTEROID_MEDIUM, GAME_COLOR_ASTEROID_LARGE
};
static const uint16_t asteroid_angle_step[4] = {8192, 7282, 6553, 5957};   /* 65536 / (8..11) */

static const int8_t ship_shape[4][2] = {{8, 0}, {-8, -4}, {-4, 0}, {-8, 4}};

/* Power-up icon: an octagon of radius 7 (also used, larger, for the shield ring) */
static const int8_t octagon[8][2] = {{7, 0}, {5, 5}, {0, 7}, {-5, 5}, {-7, 0}, {-5, -5}, {0, -7}, {5, -5}};
static const uint8_t powerup_color_table[GAME_POWERUP_TYPES] = {
    GAME_COLOR_SHIP, GAME_COLOR_RED, GAME_COLOR_MAGENTA, GAME_COLOR_YELLOW
};
/* stars drift left this fast per 50 Hz frame (Lovable: 0.1 / 0.3 / 0.6 of 10 px/s at 800 wide) */
static const int32_t star_speed_table[STAR_LAYERS] = {524, 1573, 3146};
static const uint8_t star_color_table[STAR_LAYERS] = {GAME_COLOR_STAR_DIM, GAME_COLOR_GREY, GAME_COLOR_STAR_BRIGHT};

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

static void emit(GameState *state, int sfx) {
    state->sound_events |= (uint16_t) (1u << sfx);
}

uint16_t game_take_sound_events(GameState *state) {
    const uint16_t events = state->sound_events;

    state->sound_events = 0;
    return events;
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

/* The shared rock outlines: GAME_ROCK_SHAPES per size, made once from a fixed seed with the same recipe as
   the old per-rock random outlines (8-11 corners, radii within 20% of the size's radius). */
static struct {
    uint8_t point_count;
    uint8_t radius[GAME_MAX_ASTEROID_POINTS];
} rock_shapes[4][GAME_ROCK_SHAPES];
static int rock_shapes_ready;

static void ensure_rock_shapes(void) {
    uint32_t seed = 0x2545f491u;
    int size;
    int shape;
    int index;

    if (rock_shapes_ready) {
        return;
    }
    for (size = GAME_ASTEROID_SMALL; size <= GAME_ASTEROID_LARGE; ++size) {
        for (shape = 0; shape < GAME_ROCK_SHAPES; ++shape) {
            seed = seed * 1103515245u + 12345u;
            rock_shapes[size][shape].point_count = (uint8_t) (8 + ((seed >> 16) & 3));
            for (index = 0; index < rock_shapes[size][shape].point_count; ++index) {
                seed = seed * 1103515245u + 12345u;
                rock_shapes[size][shape].radius[index] =
                    (uint8_t) ((asteroid_radius_table[size] * (205 + ((seed >> 16) & 0x7fffu) % 103)) >> 8);
            }
        }
    }
    rock_shapes_ready = 1;
}

void game_rock_shape(int size, int shape, GameAsteroid *out) {
    ensure_rock_shapes();
    memset(out, 0, sizeof(*out));
    out->size = (uint8_t) size;
    out->shape = (uint8_t) (shape + 1);
    out->point_count = rock_shapes[size][shape].point_count;
    memcpy(out->radius, rock_shapes[size][shape].radius, sizeof(out->radius));
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
    {
        /* the random outline above only decides which of the shared outlines this rock gets (so the random
           sequence of the game is what it always was) */
        unsigned hash = asteroid->point_count;
        GameAsteroid shared;

        for (index = 0; index < asteroid->point_count; ++index) {
            hash = hash * 3u + asteroid->radius[index];
        }
        game_rock_shape(size, (int) (hash & (GAME_ROCK_SHAPES - 1)), &shared);
        asteroid->shape = shared.shape;
        asteroid->point_count = shared.point_count;
        memcpy(asteroid->radius, shared.radius, sizeof(asteroid->radius));
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

static void start_boss(GameState *state);

static void spawn_wave(GameState *state) {
    int count = WAVE_BASE_ASTEROIDS + 2 * state->wave;
    int index;

    memset(state->enemy_bullets, 0, sizeof(state->enemy_bullets));
    state->alien_timer = (int16_t) (500 - state->wave * 17 > 250 ? 500 - state->wave * 17 : 250);
    if (state->wave % 5 == 0) {
        /* every fifth wave is a boss fight: no rocks */
        start_boss(state);
        emit(state, SFX_WAVE_START);
        return;
    }

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
    emit(state, SFX_WAVE_START);
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
    state->score += (state->multiplier_timer > 0) ? points * 2u : points;
    while (state->score >= state->next_extra_life) {
        if (state->lives < MAX_LIVES) {
            ++state->lives;
            emit(state, SFX_EXTRA_LIFE);
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
    emit(state, SFX_HYPERSPACE);
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
            state->ship.cooldown = (state->rapid_timer > 0) ? RAPID_FIRE_COOLDOWN : SHIP_COOLDOWN_FRAMES;
            emit(state, SFX_SHOOT);
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

void game_update_bullets_ref(GameState *state) {
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

/* For st_step.S, which does the moving and wrapping and calls back here for the rare wrap-around nudge. */
void game_nudge_rock(const GameState *state, GameAsteroid *asteroid) {
    nudge_towards_ship(state, asteroid);
}

void game_update_rocks_ref(GameState *state) {
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

/* The three loops that are worth doing in assembly on the Atari; the C versions are the reference. */
static void update_bullets(GameState *state) {
#ifdef ATARI_ST_TARGET
    st_update_bullets(state);
#else
    game_update_bullets_ref(state);
#endif
}

static void update_asteroids(GameState *state) {
#ifdef ATARI_ST_TARGET
    st_update_rocks(state);
#else
    game_update_rocks_ref(state);
#endif
}

/* ---- power-ups and stars ---- */

static void spawn_powerup(GameState *state, int32_t x, int32_t y, int type) {
    int index;

    for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
        GamePowerUp *powerup = &state->powerups[index];
        if (!powerup->active) {
            powerup->active = 1;
            powerup->type = (uint8_t) type;
            powerup->x = x;
            powerup->y = y;
            /* drifts at up to 0.12 px/frame in each direction (Lovable: 0.25 px/frame at 60 Hz, x0.4x1.2 x0.5) */
            powerup->vx = (int32_t) game_rand_below(state, 15729) - 7864;
            powerup->vy = (int32_t) game_rand_below(state, 15729) - 7864;
            powerup->life = POWERUP_LIFE_FRAMES;
            return;
        }
    }
}

static void maybe_drop_powerup(GameState *state, int32_t x, int32_t y) {
    if (game_rand_below(state, 100) < POWERUP_DROP_PERCENT) {
        spawn_powerup(state, x, y, game_rand_below(state, GAME_POWERUP_TYPES));
    }
}

static void collect_powerup(GameState *state, int type) {
    switch (type) {
    case GAME_POWERUP_SHIELD:
        state->shield_timer = SHIELD_FRAMES;
        break;
    case GAME_POWERUP_RAPID_FIRE:
        state->rapid_timer = RAPID_FIRE_FRAMES;
        break;
    case GAME_POWERUP_EXTRA_LIFE:
        if (state->lives < MAX_LIVES) {
            ++state->lives;
        }
        break;
    default:
        state->multiplier_timer = MULTIPLIER_FRAMES;
        break;
    }
    emit(state, type == GAME_POWERUP_EXTRA_LIFE ? SFX_EXTRA_LIFE : SFX_POWERUP);
}

static void update_powerups(GameState *state) {
    int index;

    for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
        GamePowerUp *powerup = &state->powerups[index];
        if (!powerup->active) {
            continue;
        }
        powerup->x += powerup->vx;
        powerup->y += powerup->vy;
        wrap_world(&powerup->x, &powerup->y);
        if (powerup->life > 0) {
            --powerup->life;
        }
        if (within_radius(powerup->x, powerup->y, state->ship.x, state->ship.y, POWERUP_RADIUS + 4)) {
            collect_powerup(state, powerup->type);
            powerup->active = 0;
        } else if (powerup->life == 0) {
            powerup->active = 0;
        }
    }

    if (state->shield_timer > 0) {
        --state->shield_timer;
    }
    if (state->rapid_timer > 0) {
        --state->rapid_timer;
    }
    if (state->multiplier_timer > 0) {
        --state->multiplier_timer;
    }
}

static void init_stars(GameState *state) {
    int index;

    for (index = 0; index < GAME_STAR_COUNT; ++index) {
        GameStar *star = &state->stars[index];
        const int32_t world_y = (int32_t) game_rand_below(state, GAME_WORLD_HEIGHT) << GAME_FIX_SHIFT;

        star->x = (int32_t) game_rand_below(state, GAME_WORLD_WIDTH) << GAME_FIX_SHIFT;
        star->layer = (uint8_t) (index / (GAME_STAR_COUNT / STAR_LAYERS));
        star->key = (int16_t) (star->x >> 13);
        star->stale_frames = 0;
        star->fresh_frames = 0;
        state->star_points[index * 2] = (int16_t) screen_x(state, star->x);
        state->star_points[index * 2 + 1] = (int16_t) screen_y(state, world_y);
    }
}

/* Drift left. A star's screen pixel is only worked out when it has moved an eighth of a pixel,
   and its old pixel is remembered for erasing when it actually changes. */
static void update_stars(GameState *state) {
    int index;

    /* they crawl (a pixel every 2-20 frames), so every fourth frame at four times the step is enough */
    if ((state->frame & 3) != 0) {
        return;
    }
    for (index = 0; index < GAME_STAR_COUNT; ++index) {
        GameStar *star = &state->stars[index];
        int16_t key;

        star->x -= star_speed_table[star->layer] * 4;
        if (star->x < 0) {
            star->x += (int32_t) GAME_WORLD_WIDTH << GAME_FIX_SHIFT;
        }
        key = (int16_t) (star->x >> 13);
        if (key != star->key) {
            const int16_t new_x = (int16_t) screen_x(state, star->x);

            star->key = key;
            if (new_x != state->star_points[index * 2]) {
                star->stale_x = state->star_points[index * 2];
                star->stale_y = state->star_points[index * 2 + 1];
                star->stale_frames = 2;   /* the old pixel is on both screen buffers */
                star->fresh_frames = 2;   /* and the new one has to get onto both */
                state->star_points[index * 2] = new_x;
            }
        }
    }
}

static void lose_life(GameState *state);

/* ---- enemies: UFOs, aliens and bosses ---- */

/* Lovable speeds are px/frame at 60 Hz on an 800 px canvas; x0.48 gives world px per 50 Hz frame (16.16). */
#define UFO_SPEED_MIN 31457L                 /* 1.0 */
#define UFO_SPEED_SPREAD 15729               /* +0..0.5 */
#define UFO_WAVE_STEP 375                    /* vertical wobble phase per frame (0.036 rad) */
#define UFO_WOBBLE 37749L                    /* 1.2 */
#define UFO_BULLET_SPEED 157286L             /* 5 */
#define GREEN_BULLET_SPEED 188744L           /* 6 */
#define BLUE_BULLET_SPEED 110100L            /* 3.5 */
#define ENEMY_BULLET_LIFE 50
#define ENEMY_MARGIN 30                      /* how far off the field an enemy may wander before it is gone */
#define BOSS_ENTER_SPEED 24000L              /* 0.8 px per 60 Hz frame */
#define BOSS_HIT_FLASH 10
#define BOSS_BAR_WIDTH 208
#define MAX_UFO_MINIONS 3
#define ENEMY_ENTER_LEFT (-8)
#define ENEMY_ENTER_RIGHT 328

static const uint8_t enemy_radius_table[7] = {0, 8, 4, 5, 5, 5, 3};
static const uint16_t enemy_points_table[7] = {0, 200, 1000, 300, 500, 600, 100};
static const uint8_t enemy_color_table[7] = {
    0, GAME_COLOR_YELLOW, GAME_COLOR_MAGENTA, GAME_COLOR_BROWN, GAME_COLOR_SHIP, GAME_COLOR_ASTEROID_SMALL,
    GAME_COLOR_PURPLE
};
static const uint8_t enemy_hp_table[7] = {0, 1, 1, 1, 2, 2, 1};
static const uint8_t boss_radius_table[5] = {0, 20, 22, 24, 20};
static const uint8_t boss_color_table[5] = {0, GAME_COLOR_RED, GAME_COLOR_ASTEROID_SMALL, GAME_COLOR_MAGENTA,
                                            GAME_COLOR_CYAN};
static const uint8_t boss_bullet_color[5] = {0, GAME_COLOR_RED, GAME_COLOR_YELLOW, GAME_COLOR_MAGENTA,
                                             GAME_COLOR_CYAN};

/* Unit vector from (dx, dy) in Q14; both zero when the two points coincide. */
static void normalize_q14(int32_t dx, int32_t dy, int32_t *nx, int32_t *ny) {
    const int32_t distance = (int32_t) isqrt32((uint32_t) ((dx * dx) + (dy * dy)));

    if (distance == 0) {
        *nx = 0;
        *ny = 0;
        return;
    }
    *nx = (dx << 14) / distance;
    *ny = (dy << 14) / distance;
}

/* Rotate a Q14 vector by an angle in 1/65536 turns. */
static void rotate_q14(int32_t *x, int32_t *y, uint16_t angle) {
    const int32_t c = trig_cos(angle);
    const int32_t s = trig_sin(angle);
    const int32_t rx = (mul16((int16_t) *x, (int16_t) c) - mul16((int16_t) *y, (int16_t) s)) >> 14;
    const int32_t ry = (mul16((int16_t) *x, (int16_t) s) + mul16((int16_t) *y, (int16_t) c)) >> 14;

    *x = rx;
    *y = ry;
}

static int32_t scaled_by_wave(const GameState *state, int32_t value) {
    const int wave = (state->wave > SPEED_SCALE_MAX_WAVE) ? SPEED_SCALE_MAX_WAVE : state->wave;

    return ((value >> 4) * (256 + SPEED_SCALE_STEP * (wave - 1))) >> 4;
}

static void fire_enemy_bullet(GameState *state, int32_t x, int32_t y, int32_t vx, int32_t vy, uint8_t color, uint8_t life) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        GameBullet *bullet = &state->enemy_bullets[index];
        if (!bullet->active) {
            bullet->active = 1;
            bullet->life = life;
            bullet->color = color;
            bullet->x = x;
            bullet->y = y;
            bullet->vx = vx;
            bullet->vy = vy;
            emit(state, SFX_ENEMY_SHOT);
            return;
        }
    }
}

/* A shot from (x, y) towards the ship, rotated by `error` (1/65536 turn), at `speed` (16.16). */
static void aim_enemy_bullet(GameState *state, int32_t x, int32_t y, int32_t speed, uint16_t error, uint8_t color, uint8_t life) {
    int32_t nx;
    int32_t ny;

    normalize_q14((state->ship.x - x) >> GAME_FIX_SHIFT, (state->ship.y - y) >> GAME_FIX_SHIFT, &nx, &ny);
    if (nx == 0 && ny == 0) {
        nx = 16384;
    }
    rotate_q14(&nx, &ny, error);
    fire_enemy_bullet(state, x, y, trig_mul(speed, nx), trig_mul(speed, ny), color, life);
}

static GameEnemy *spawn_enemy(GameState *state, uint8_t kind, int32_t x, int32_t y, int32_t vx, int32_t vy) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        GameEnemy *enemy = &state->enemies[index];
        if (!enemy->active) {
            memset(enemy, 0, sizeof(*enemy));
            enemy->active = 1;
            enemy->kind = kind;
            enemy->hp = enemy_hp_table[kind];
            enemy->x = x;
            enemy->y = y;
            enemy->vx = vx;
            enemy->vy = vy;
            enemy->phase = game_rand16(state);
            return enemy;
        }
    }
    return NULL;
}

static int count_enemies(const GameState *state, uint8_t kind) {
    int index;
    int count = 0;

    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        count += (state->enemies[index].active && (kind == 0 || state->enemies[index].kind == kind));
    }
    return count;
}

static void spawn_ufo(GameState *state) {
    const int from_left = game_rand_below(state, 2);
    const uint8_t kind = (state->wave > 6 && game_rand_below(state, 2)) ? ENEMY_UFO_SMALL : ENEMY_UFO_LARGE;
    const int32_t speed = UFO_SPEED_MIN + game_rand_below(state, UFO_SPEED_SPREAD);
    GameEnemy *ufo = spawn_enemy(state, kind, from_left ? 0 : ((int32_t) GAME_WORLD_WIDTH << GAME_FIX_SHIFT),
                                 (int32_t) (20 + game_rand_below(state, GAME_WORLD_HEIGHT - 40)) << GAME_FIX_SHIFT,
                                 from_left ? speed : -speed, 0);

    if (ufo != NULL) {
        ufo->timer = (int16_t) (50 + game_rand_below(state, 50));
    }
}

static void spawn_alien(GameState *state, uint8_t kind) {
    const int from_left = game_rand_below(state, 2);
    const int32_t x = (int32_t) (from_left ? ENEMY_ENTER_LEFT : ENEMY_ENTER_RIGHT) << GAME_FIX_SHIFT;
    const int32_t y = (int32_t) (12 + game_rand_below(state, GAME_WORLD_HEIGHT - 24)) << GAME_FIX_SHIFT;
    const int32_t direction = from_left ? 1 : -1;
    GameEnemy *alien;

    switch (kind) {
    case ENEMY_BROWN:
        alien = spawn_enemy(state, kind, x, y, direction * (62914L + game_rand_below(state, 47186)), 0);
        break;
    case ENEMY_GREEN: {
        const int32_t speed = 25166L + game_rand_below(state, 12583);
        alien = spawn_enemy(state, kind, x, y, direction * speed, (int32_t) (game_rand_below(state, 1000) - 500) * speed / 2000);
        if (alien != NULL) {
            alien->timer = (int16_t) (75 + game_rand_below(state, 50));
        }
        break;
    }
    case ENEMY_BLUE:
        alien = spawn_enemy(state, kind, x, y, direction * 15729L, 0);
        if (alien != NULL) {
            alien->timer = (int16_t) (50 + game_rand_below(state, 33));
            alien->timer2 = (int16_t) (75 + game_rand_below(state, 50));   /* until the first teleport */
        }
        break;
    default:
        alien = NULL;
        break;
    }
    (void) alien;
}

static void spawn_swarm(GameState *state) {
    const int from_left = game_rand_below(state, 2);
    const int32_t x = (int32_t) (from_left ? ENEMY_ENTER_LEFT : ENEMY_ENTER_RIGHT) << GAME_FIX_SHIFT;
    const int32_t base_y = (int32_t) (24 + game_rand_below(state, GAME_WORLD_HEIGHT - 48)) << GAME_FIX_SHIFT;
    const uint8_t flock = (uint8_t) (1 + game_rand_below(state, 250));
    int member;

    for (member = 0; member < 3; ++member) {
        const int32_t speed = 12583L + game_rand_below(state, 9437);
        GameEnemy *alien = spawn_enemy(state, ENEMY_PURPLE, x, base_y + (int32_t) (member - 1) * (10L << GAME_FIX_SHIFT),
                                       from_left ? speed : -speed, (int32_t) game_rand_below(state, 31457) - 15729);
        if (alien != NULL) {
            alien->flock = flock;
        }
    }
}

static void kill_enemy(GameState *state, GameEnemy *enemy) {
    add_score(state, enemy_points_table[enemy->kind]);
    emit(state, SFX_EXPLODE_MEDIUM);
    maybe_drop_powerup(state, enemy->x, enemy->y);
    enemy->active = 0;
}

/* Per-kind behaviour for one frame. */
static void update_enemy(GameState *state, GameEnemy *enemy) {
    const int32_t dx = (state->ship.x - enemy->x) >> GAME_FIX_SHIFT;
    const int32_t dy = (state->ship.y - enemy->y) >> GAME_FIX_SHIFT;
    int32_t nx;
    int32_t ny;
    int32_t vx8;
    int32_t vy8;
    int32_t speed;
    int32_t max_speed;

    ++enemy->age;
    enemy->phase = (uint16_t) (enemy->phase + 626);   /* 0.05 rad per 60 Hz frame */

    switch (enemy->kind) {
    case ENEMY_UFO_LARGE:
    case ENEMY_UFO_SMALL:
        enemy->vy = trig_mul(UFO_WOBBLE, trig_sin((uint16_t) (enemy->age * UFO_WAVE_STEP)));
        if (--enemy->timer <= 0) {
            enemy->timer = (int16_t) (50 + game_rand_below(state, 50));
            if (enemy->kind == ENEMY_UFO_SMALL) {
                aim_enemy_bullet(state, enemy->x, enemy->y, UFO_BULLET_SPEED, 0, GAME_COLOR_RED, ENEMY_BULLET_LIFE);
            } else {
                const uint16_t heading = game_rand16(state);
                fire_enemy_bullet(state, enemy->x, enemy->y, trig_mul(UFO_BULLET_SPEED, trig_cos(heading)),
                                  trig_mul(UFO_BULLET_SPEED, trig_sin(heading)), GAME_COLOR_RED, ENEMY_BULLET_LIFE);
            }
        }
        break;

    case ENEMY_BROWN:
        /* chases the ship, weaving: the direction is swung by up to +-0.8 rad (8340 turns/65536) */
        normalize_q14(dx, dy, &nx, &ny);
        rotate_q14(&nx, &ny, (uint16_t) (trig_sin((uint16_t) (enemy->phase * 3)) * 8340 >> 14));
        enemy->vx += trig_mul(SHIP_THRUST, nx);
        enemy->vy += trig_mul(SHIP_THRUST, ny);
        vx8 = enemy->vx >> 8;
        vy8 = enemy->vy >> 8;
        speed = (int32_t) isqrt32((uint32_t) ((vx8 * vx8) + (vy8 * vy8)));
        max_speed = scaled_by_wave(state, 94372L) >> 8;   /* 3 px/frame at 60 Hz */
        if (speed > max_speed && speed > 0) {
            enemy->vx = (enemy->vx * max_speed) / speed;
            enemy->vy = (enemy->vy * max_speed) / speed;
        }
        break;

    case ENEMY_GREEN:
        enemy->vx -= (enemy->vx / 256) * SHIP_DRAG_NUMERATOR;
        enemy->vy += trig_mul(755, trig_sin(enemy->phase));
        if (--enemy->timer <= 0) {
            const int wave_bonus = state->wave * 2;
            enemy->timer = (int16_t) ((67 - wave_bonus > 33 ? 67 - wave_bonus : 33) + game_rand_below(state, 33));
            aim_enemy_bullet(state, enemy->x, enemy->y, GREEN_BULLET_SPEED, (uint16_t) (game_rand_below(state, 3651) - 1826),
                             GAME_COLOR_SHIP, ENEMY_BULLET_LIFE);
        }
        break;

    case ENEMY_BLUE:
        enemy->vx -= (enemy->vx / 256) * 13;   /* x0.95 */
        enemy->vy -= (enemy->vy / 256) * 13;
        if (enemy->flags & 1) {                                  /* invisible after a burst */
            if (--enemy->timer2 <= 0) {
                enemy->flags &= (uint8_t) ~1u;
                enemy->timer2 = (int16_t) (75 + game_rand_below(state, 50));
            }
        } else {
            if (enemy->burst < 3 && --enemy->timer <= 0) {
                const uint16_t spread = (uint16_t) ((int) (enemy->burst - 1) * 1640);   /* +-0.15 rad */

                ++enemy->burst;
                enemy->timer = 12;
                aim_enemy_bullet(state, enemy->x, enemy->y, BLUE_BULLET_SPEED, spread, GAME_COLOR_ASTEROID_SMALL,
                                 ENEMY_BULLET_LIFE);
                if (enemy->burst >= 3) {
                    enemy->burst = 0;
                    enemy->timer = (int16_t) (100 + game_rand_below(state, 50));
                    enemy->flags |= 1;
                    enemy->timer2 = 50;
                }
            }
            if (enemy->timer2 > 0 && !(enemy->flags & 2) && --enemy->timer2 <= 0) {
                enemy->flags |= 2;        /* charging up to teleport */
                enemy->timer2 = 25;
            } else if (enemy->flags & 2) {
                enemy->vx = 0;
                enemy->vy = 0;
                if (--enemy->timer2 <= 0) {
                    enemy->x = (int32_t) (20 + game_rand_below(state, GAME_WORLD_WIDTH - 40)) << GAME_FIX_SHIFT;
                    enemy->y = (int32_t) (20 + game_rand_below(state, GAME_WORLD_HEIGHT - 40)) << GAME_FIX_SHIFT;
                    enemy->flags &= (uint8_t) ~2u;
                    enemy->timer2 = (int16_t) (170 + game_rand_below(state, 100));
                    emit(state, SFX_HYPERSPACE);
                }
            }
        }
        break;

    default: {   /* purple swarm: flocks towards a blend of its mates' centre and the ship */
        int32_t cx = 0;
        int32_t cy = 0;
        int mates = 0;
        int other;

        for (other = 0; other < GAME_MAX_ENEMIES; ++other) {
            const GameEnemy *mate = &state->enemies[other];
            if (mate != enemy && mate->active && mate->kind == ENEMY_PURPLE && mate->flock == enemy->flock) {
                cx += mate->x >> GAME_FIX_SHIFT;
                cy += mate->y >> GAME_FIX_SHIFT;
                ++mates;
                {
                    const int32_t sx = (enemy->x - mate->x) >> GAME_FIX_SHIFT;
                    const int32_t sy = (enemy->y - mate->y) >> GAME_FIX_SHIFT;
                    if (sx * sx + sy * sy < 100 && (sx != 0 || sy != 0)) {   /* closer than 10: push apart */
                        normalize_q14(sx, sy, &nx, &ny);
                        enemy->vx += trig_mul(9437, nx);    /* 0.3 */
                        enemy->vy += trig_mul(9437, ny);
                    }
                }
            }
        }
        if (mates > 0) {
            cx = ((cx + (enemy->x >> GAME_FIX_SHIFT)) / (mates + 1)) * 4 / 10 + (state->ship.x >> GAME_FIX_SHIFT) * 6 / 10;
            cy = ((cy + (enemy->y >> GAME_FIX_SHIFT)) / (mates + 1)) * 4 / 10 + (state->ship.y >> GAME_FIX_SHIFT) * 6 / 10;
            normalize_q14(cx - (enemy->x >> GAME_FIX_SHIFT), cy - (enemy->y >> GAME_FIX_SHIFT), &nx, &ny);
        } else {
            normalize_q14(dx, dy, &nx, &ny);
        }
        enemy->vx += trig_mul(566, nx);
        enemy->vy += trig_mul(566, ny);
        vx8 = enemy->vx >> 8;
        vy8 = enemy->vy >> 8;
        speed = (int32_t) isqrt32((uint32_t) ((vx8 * vx8) + (vy8 * vy8)));
        max_speed = scaled_by_wave(state, 25166L) >> 8;   /* 0.8 px/frame at 60 Hz */
        if (speed > max_speed && speed > 0) {
            enemy->vx = (enemy->vx * max_speed) / speed;
            enemy->vy = (enemy->vy * max_speed) / speed;
        }
        break;
    }
    }

    enemy->x += enemy->vx;
    enemy->y += enemy->vy;
    if (enemy->kind >= ENEMY_BROWN) {
        /* aliens wrap top and bottom, and give up once they have wandered well off the sides */
        if (enemy->y < 0) {
            enemy->y += (int32_t) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT;
        } else if (enemy->y >= ((int32_t) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT)) {
            enemy->y -= (int32_t) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT;
        }
        if (enemy->age > 100 && (enemy->x < -((int32_t) ENEMY_MARGIN << GAME_FIX_SHIFT) ||
                                 enemy->x > ((int32_t) (GAME_WORLD_WIDTH + ENEMY_MARGIN) << GAME_FIX_SHIFT))) {
            enemy->active = 0;
        }
    } else if (enemy->x < -((int32_t) 24 << GAME_FIX_SHIFT) || enemy->x > ((int32_t) (GAME_WORLD_WIDTH + 24) << GAME_FIX_SHIFT)) {
        enemy->active = 0;   /* a UFO that has crossed the field flies off */
    }
}

static void update_enemy_bullets(GameState *state) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        GameBullet *bullet = &state->enemy_bullets[index];
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

/* ---- bosses (waves 5, 10, 15, 20, ...: Amiga ball, flying saucer, tentacle, Borg cube) ---- */

static void start_boss(GameState *state) {
    GameBoss *boss = &state->boss;
    const int kind = 1 + (state->wave / 5 - 1) % 4;
    const int base_hp = 3 + state->wave;

    memset(boss, 0, sizeof(*boss));
    boss->active = 1;
    boss->kind = (uint8_t) kind;
    boss->x = ((int32_t) GAME_WORLD_WIDTH / 2) << GAME_FIX_SHIFT;
    boss->y = -(40L << GAME_FIX_SHIFT);
    boss->hp = (int16_t) (kind == BOSS_AMIGA_BALL ? base_hp * 12 / 10 : kind == BOSS_FLYING_SAUCER ? base_hp * 15 / 10
                          : kind == BOSS_BORG_CUBE ? base_hp * 18 / 10 : base_hp);
    boss->max_hp = boss->hp;
    boss->timer = 120;
    boss->timer2 = 250;
    switch (kind) {
    case BOSS_AMIGA_BALL:
        boss->target_y = 100L << GAME_FIX_SHIFT;
        boss->vx = (31457L + game_rand_below(state, 31457)) * (game_rand_below(state, 2) ? 1 : -1);
        boss->vy = -94372L;
        break;
    case BOSS_FLYING_SAUCER:
        boss->target_y = 48L << GAME_FIX_SHIFT;
        break;
    case BOSS_BORG_CUBE:
        boss->target_y = 52L << GAME_FIX_SHIFT;
        break;
    default:
        boss->target_y = (int32_t) (40 + game_rand_below(state, 24)) << GAME_FIX_SHIFT;
        boss->vx = 15729L;
        break;
    }
    state->banner_timer = BANNER_FRAMES;
}

static void spawn_boss_minion(GameState *state, const GameBoss *boss) {
    if (count_enemies(state, ENEMY_UFO_SMALL) < MAX_UFO_MINIONS) {
        const int32_t speed = UFO_SPEED_MIN + game_rand_below(state, UFO_SPEED_SPREAD);
        GameEnemy *minion = spawn_enemy(state, ENEMY_UFO_SMALL, boss->x, boss->y, game_rand_below(state, 2) ? speed : -speed, 0);

        if (minion != NULL) {
            minion->timer = (int16_t) (50 + game_rand_below(state, 50));
        }
    }
}

static void update_boss(GameState *state) {
    GameBoss *boss = &state->boss;
    const uint8_t color = boss_bullet_color[boss->kind];
    int index;

    boss->phase = (uint16_t) (boss->phase + 250);        /* 0.02 rad per 60 Hz frame */
    boss->angle = (uint16_t) (boss->angle + 100);        /* spin */
    if (boss->flash > 0) {
        --boss->flash;
    }

    if (!boss->entered) {
        boss->y += BOSS_ENTER_SPEED;
        if (boss->y >= boss->target_y) {
            boss->entered = 1;
        }
        return;
    }

    switch (boss->kind) {
    case BOSS_AMIGA_BALL:
        boss->vy += 944;   /* gravity */
        boss->x += boss->vx;
        boss->y += boss->vy;
        if (boss->x < (24L << GAME_FIX_SHIFT) || boss->x > (296L << GAME_FIX_SHIFT)) {
            boss->vx = -boss->vx;
            boss->x = (boss->x < (24L << GAME_FIX_SHIFT)) ? (24L << GAME_FIX_SHIFT) : (296L << GAME_FIX_SHIFT);
        }
        if (boss->y > (208L << GAME_FIX_SHIFT)) {
            boss->vy = -((boss->vy < 0 ? -boss->vy : boss->vy) / 2 + 15729L + game_rand_below(state, 62914));
            boss->y = 208L << GAME_FIX_SHIFT;
            emit(state, SFX_EXPLODE_LARGE);
        }
        if (boss->y < (16L << GAME_FIX_SHIFT)) {
            boss->vy = boss->vy < 0 ? -boss->vy : boss->vy;
            boss->y = 16L << GAME_FIX_SHIFT;
        }
        if (--boss->timer <= 0) {
            boss->timer = (int16_t) ((150 - state->wave * 2 > 75 ? 150 - state->wave * 2 : 75));
            for (index = 0; index < 8; ++index) {
                const uint16_t heading = (uint16_t) (index * 8192);
                fire_enemy_bullet(state, boss->x, boss->y, trig_mul(110100L, trig_cos(heading)),
                                  trig_mul(110100L, trig_sin(heading)), color, ENEMY_BULLET_LIFE);
            }
        }
        break;

    case BOSS_FLYING_SAUCER:
        boss->x = ((int32_t) GAME_WORLD_WIDTH << GAME_FIX_SHIFT) / 2 + (trig_sin(boss->phase) * 100L << (GAME_FIX_SHIFT - 14));
        boss->y = boss->target_y;
        if (--boss->timer <= 0) {
            boss->timer = 50;
            for (index = -1; index <= 1; ++index) {
                aim_enemy_bullet(state, boss->x, boss->y, UFO_BULLET_SPEED, (uint16_t) (index * 2600), color, ENEMY_BULLET_LIFE);
            }
        }
        if (--boss->timer2 <= 0) {
            boss->timer2 = 250;
            spawn_boss_minion(state, boss);
        }
        break;

    case BOSS_BORG_CUBE:
        boss->x = ((int32_t) GAME_WORLD_WIDTH << GAME_FIX_SHIFT) / 2 + (trig_sin((uint16_t) (boss->phase * 2)) * 90L << (GAME_FIX_SHIFT - 14));
        boss->y = boss->target_y + (trig_sin(boss->phase) * 8L << (GAME_FIX_SHIFT - 14));
        if (--boss->timer <= 0) {
            boss->timer = 40;
            aim_enemy_bullet(state, boss->x, boss->y, UFO_BULLET_SPEED, 0, color, ENEMY_BULLET_LIFE);
            aim_enemy_bullet(state, boss->x, boss->y, UFO_BULLET_SPEED, 1200, color, ENEMY_BULLET_LIFE);
        }
        if (--boss->timer2 <= 0) {
            boss->timer2 = 250;
            if (boss->hp < boss->max_hp) {
                ++boss->hp;   /* the Borg adapt: it repairs itself */
            }
            spawn_boss_minion(state, boss);
        }
        break;

    default:   /* tentacle */
        boss->x += boss->vx;
        if (boss->x < (40L << GAME_FIX_SHIFT) || boss->x > (280L << GAME_FIX_SHIFT)) {
            boss->vx = -boss->vx;
        }
        boss->y = boss->target_y + (trig_sin(boss->phase) * 12L << (GAME_FIX_SHIFT - 14));
        if (--boss->timer <= 0) {
            const int rate = 75 - (state->wave * 5) / 2;
            boss->timer = (int16_t) (rate > 33 ? rate : 33);
            for (index = -1; index <= 1; ++index) {
                aim_enemy_bullet(state, boss->x, boss->y, 125829L, (uint16_t) (index * 3300), color, 75);
            }
        }
        break;
    }
}

static void defeat_boss(GameState *state) {
    GameBoss *boss = &state->boss;
    int index;

    add_score(state, 2000u + 200u * (uint32_t) state->wave);
    emit(state, SFX_EXPLODE_LARGE);
    spawn_powerup(state, boss->x, boss->y, game_rand_below(state, GAME_POWERUP_TYPES));
    for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
        if (state->powerups[index].active && state->powerups[index].x == boss->x) {
            state->powerups[index].life = 500;   /* the reward stays around longer */
        }
    }
    boss->active = 0;
    memset(state->enemies, 0, sizeof(state->enemies));           /* its minions go with it */
    memset(state->enemy_bullets, 0, sizeof(state->enemy_bullets));
}

static void update_enemies(GameState *state) {
    int index;

    /* spawning: none during a boss fight */
    if (!state->boss.active) {
        if (--state->ufo_timer <= 0) {
            state->ufo_timer = (int16_t) ((750 - state->wave * 25 > 333 ? 750 - state->wave * 25 : 333) + game_rand_below(state, 333));
            if (count_enemies(state, ENEMY_UFO_LARGE) + count_enemies(state, ENEMY_UFO_SMALL) == 0) {
                spawn_ufo(state);
            }
        }
        if (--state->alien_timer <= 0) {
            if (state->wave >= 3 && count_enemies(state, ENEMY_BROWN) < 3) {
                spawn_alien(state, ENEMY_BROWN);
            }
            if (state->wave >= 6 && count_enemies(state, ENEMY_GREEN) < 2) {
                spawn_alien(state, ENEMY_GREEN);
            }
            if (state->wave >= 8 && count_enemies(state, ENEMY_BLUE) < 1) {
                spawn_alien(state, ENEMY_BLUE);
            }
            if (state->wave >= 12 && count_enemies(state, ENEMY_PURPLE) < 3) {
                spawn_swarm(state);
            }
            state->alien_timer = (int16_t) ((417 - state->wave * 17 > 150 ? 417 - state->wave * 17 : 150) + game_rand_below(state, 167));
        }
    } else {
        update_boss(state);
    }

    state->ufo_present = 0;
    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        GameEnemy *enemy = &state->enemies[index];
        if (enemy->active) {
            update_enemy(state, enemy);
            if (enemy->active && (enemy->kind == ENEMY_UFO_LARGE || enemy->kind == ENEMY_UFO_SMALL)) {
                state->ufo_present = 1;
            }
        }
    }
    if (state->boss.active && state->boss.kind == BOSS_FLYING_SAUCER) {
        state->ufo_present = 1;
    }
    update_enemy_bullets(state);
}

static uint8_t any_enemies(const GameState *state) {
    return (uint8_t) (state->boss.active || count_enemies(state, 0) > 0);
}

/* The player's bullets against enemies and the boss. */
static void resolve_bullets_vs_enemies(GameState *state) {
    int bullet_index;
    int enemy_index;
    int enemies_in_play = 0;

    for (enemy_index = 0; enemy_index < GAME_MAX_ENEMIES; ++enemy_index) {
        enemies_in_play |= state->enemies[enemy_index].active;
    }
    if (!enemies_in_play && !state->boss.active) {
        return;
    }

    for (bullet_index = 0; bullet_index < GAME_MAX_BULLETS; ++bullet_index) {
        GameBullet *bullet = &state->bullets[bullet_index];
        if (!bullet->active) {
            continue;
        }
        for (enemy_index = 0; enemy_index < GAME_MAX_ENEMIES; ++enemy_index) {
            GameEnemy *enemy = &state->enemies[enemy_index];
            if (!enemy->active || (enemy->flags & 1)) {
                continue;   /* an invisible sentinel cannot be hit */
            }
            if (within_radius(bullet->x, bullet->y, enemy->x, enemy->y, enemy_radius_table[enemy->kind] + BULLET_RADIUS)) {
                bullet->active = 0;
                if (--enemy->hp <= 0) {
                    kill_enemy(state, enemy);
                } else {
                    emit(state, SFX_BOSS_HIT);
                }
                break;
            }
        }
        if (bullet->active && state->boss.active && state->boss.entered &&
            within_radius(bullet->x, bullet->y, state->boss.x, state->boss.y, boss_radius_table[state->boss.kind] + BULLET_RADIUS)) {
            bullet->active = 0;
            state->boss.flash = BOSS_HIT_FLASH;
            if (--state->boss.hp <= 0) {
                defeat_boss(state);
            } else {
                emit(state, SFX_BOSS_HIT);
            }
        }
    }
}

/* Enemies, their bullets and the boss against the ship. */
static void resolve_enemy_threats(GameState *state) {
    int index;

    if (state->ship.invulnerability > 0 || state->shield_timer > 0) {
        return;
    }
    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        const GameEnemy *enemy = &state->enemies[index];
        if (enemy->active && !(enemy->flags & 1) &&
            within_radius(state->ship.x, state->ship.y, enemy->x, enemy->y, enemy_radius_table[enemy->kind] + SHIP_RADIUS)) {
            lose_life(state);
            return;
        }
    }
    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        GameBullet *bullet = &state->enemy_bullets[index];
        if (bullet->active && within_radius(state->ship.x, state->ship.y, bullet->x, bullet->y, SHIP_RADIUS + BULLET_RADIUS)) {
            bullet->active = 0;
            lose_life(state);
            return;
        }
    }
    if (state->boss.active && state->boss.entered &&
        within_radius(state->ship.x, state->ship.y, state->boss.x, state->boss.y, boss_radius_table[state->boss.kind] + SHIP_RADIUS)) {
        lose_life(state);
    }
}

/* The first hit at or after bullet `first_bullet`: bullets in order, and for each the first rock (in slot order)
   it touches. Returns the bullet's index, or -1, and the rock's index in *rock. */
int game_find_bullet_hit_ref(const GameState *state, int first_bullet, int *rock) {
    int bullet_index;
    int asteroid_index;

    for (bullet_index = first_bullet; bullet_index < GAME_MAX_BULLETS; ++bullet_index) {
        const GameBullet *bullet = &state->bullets[bullet_index];

        if (!bullet->active) {
            continue;
        }
        for (asteroid_index = 0; asteroid_index < GAME_MAX_ASTEROIDS; ++asteroid_index) {
            const GameAsteroid *asteroid = &state->asteroids[asteroid_index];

            if (asteroid->active &&
                within_radius(bullet->x, bullet->y, asteroid->x, asteroid->y,
                              asteroid_radius_table[asteroid->size] + BULLET_RADIUS)) {
                *rock = asteroid_index;
                return bullet_index;
            }
        }
    }
    return -1;
}

static void resolve_bullet_collisions(GameState *state) {
    int bullet_index = 0;

    for (;;) {
        int rock_index = 0;
        GameBullet *bullet;
        GameAsteroid *asteroid;
        GameAsteroid exploded;

#ifdef ATARI_ST_TARGET
        {
            long rock_long = 0;

            bullet_index = (int) st_find_bullet_hit(state, bullet_index, rock_hit_reach, &rock_long);
            rock_index = (int) rock_long;
        }
#else
        bullet_index = game_find_bullet_hit_ref(state, bullet_index, &rock_index);
#endif
        if (bullet_index < 0) {
            break;
        }
        bullet = &state->bullets[bullet_index];
        asteroid = &state->asteroids[rock_index];
        exploded = *asteroid;
        bullet->active = 0;
        asteroid->active = 0;
        add_score(state, asteroid_points_table[exploded.size]);
        emit(state, exploded.size == GAME_ASTEROID_LARGE ? SFX_EXPLODE_LARGE :
                    exploded.size == GAME_ASTEROID_MEDIUM ? SFX_EXPLODE_MEDIUM : SFX_EXPLODE_SMALL);
        split_asteroid(state, &exploded);
        maybe_drop_powerup(state, exploded.x, exploded.y);
        ++bullet_index;   /* the rock went and its pieces came; carry on with the next bullet */
    }
}

static void lose_life(GameState *state) {
    if (state->lives > 0) {
        --state->lives;
    }
    reset_ship(state);
    memset(state->enemy_bullets, 0, sizeof(state->enemy_bullets));
    emit(state, SFX_SHIP_DEATH);
    if (state->lives == 0) {
        enter_mode(state, GAME_MODE_GAME_OVER);
        emit(state, SFX_GAME_OVER);
    }
}

static void resolve_ship_collisions(GameState *state) {
    int asteroid_index;

    if (state->ship.invulnerability > 0 || state->shield_timer > 0) {
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
    state->shield_timer = 0;
    state->rapid_timer = 0;
    state->multiplier_timer = 0;
    memset(state->powerups, 0, sizeof(state->powerups));
    memset(state->enemies, 0, sizeof(state->enemies));
    memset(state->enemy_bullets, 0, sizeof(state->enemy_bullets));
    memset(&state->boss, 0, sizeof(state->boss));
    memset(state->asteroids, 0, sizeof(state->asteroids));
    memset(state->bullets, 0, sizeof(state->bullets));
    reset_ship(state);
    state->rng_state ^= (uint32_t) state->frame * 2654435761u;   /* a different field every game */
    state->ufo_timer = (int16_t) (750 + game_rand_below(state, 500));
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
    init_stars(state);
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
    update_powerups(state);
    update_stars(state);
    update_enemies(state);
    resolve_bullet_collisions(state);
    resolve_bullets_vs_enemies(state);
    resolve_ship_collisions(state);
    resolve_enemy_threats(state);

    if (state->banner_timer > 0) {
        --state->banner_timer;
    }
    if (state->mode == GAME_MODE_PLAYING && !active_asteroids(state) && !any_enemies(state)) {
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
        emit(state, SFX_MENU);
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

    if (state->shield_timer > 0 && (state->shield_timer > 50 || ((state->shield_timer >> 2) & 1))) {
        /* a ring around the ship (the power-up octagon at 12/7 the size) */
        int16_t ring[16];

        for (index = 0; index < 8; ++index) {
            const int x = center_x + scale_x(state, (octagon[index][0] * 12) / 7);
            const int y = center_y + scale_y(state, (octagon[index][1] * 12) / 7);

            ring[index * 2] = (int16_t) x;
            ring[index * 2 + 1] = (int16_t) y;
            grow_bounds(&min_x, &min_y, &max_x, &max_y, x, y);
        }
        draw_outline(renderer, ring, 8, GAME_COLOR_SHIP);
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
/* Small and medium rocks are only a few pixels across, so extra vertices add drawing cost but no visible detail:
   they are drawn with 6 and 8 corners, taking evenly spaced ones of their 8-11 radii. */
static const uint8_t pick_small[4][6] = {{0, 1, 3, 4, 6, 7}, {0, 1, 3, 5, 6, 8}, {0, 2, 3, 5, 7, 8}, {0, 2, 4, 5, 7, 9}};
static const uint8_t pick_medium[4][8] = {{0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 4, 5, 6, 7, 8}, {0, 1, 3, 4, 5, 7, 8, 9},
                                          {0, 1, 3, 4, 6, 7, 8, 10}};

static void rebuild_asteroid_cache(const GameState *state, GameAsteroid *asteroid, uint8_t orientation) {
    const uint8_t *pick = NULL;
    int draw_count = asteroid->point_count;
    uint16_t step;
    uint16_t angle = (uint16_t) ((uint16_t) orientation << 10);
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    int vertex;

    if (asteroid->size == GAME_ASTEROID_SMALL) {
        pick = pick_small[asteroid->point_count - 8];
        draw_count = 6;
        step = 10922;   /* 65536 / 6 */
    } else if (asteroid->size == GAME_ASTEROID_MEDIUM && asteroid->point_count > 8) {
        pick = pick_medium[asteroid->point_count - 8];
        draw_count = 8;
        step = 8192;
    } else {
        step = asteroid_angle_step[asteroid->point_count - 8];
    }

    for (vertex = 0; vertex < draw_count; ++vertex) {
        const int16_t radius = asteroid->radius[pick != NULL ? pick[vertex] : vertex];
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
    asteroid->draw_count = (uint8_t) draw_count;
    asteroid->draw_cache_valid = 0;
    asteroid->cache_index = orientation;
    asteroid->cache_valid = 1;
}

void game_prepare_rock(const GameState *state, GameAsteroid *asteroid) {
    uint8_t orientation = (uint8_t) (asteroid->angle >> 10);

    if (asteroid->shape != 0) {
        orientation &= GAME_ROCK_ORIENT_MASK;   /* shared outlines turn in coarser steps */
    }

    if (!asteroid->cache_valid || asteroid->cache_index != orientation) {
        rebuild_asteroid_cache(state, asteroid, orientation);
    }
}

static void draw_asteroid(const GameState *state, GameAsteroid *asteroid, const GameRenderer *renderer) {
    const int center_x = screen_x(state, asteroid->x);
    const int center_y = screen_y(state, asteroid->y);
    int16_t points[GAME_MAX_ASTEROID_POINTS * 2];
    int count;
    int vertex;

    game_prepare_rock(state, asteroid);
    count = asteroid->draw_count;

    if (renderer->polygon_offsets != NULL && center_x + asteroid->bound_x0 >= (int) state->field_x &&
        center_x + asteroid->bound_x1 < (int) (state->field_x + state->field_width) &&
        center_y + asteroid->bound_y0 >= (int) state->field_y &&
        center_y + asteroid->bound_y1 < (int) (state->field_y + state->field_height)) {
        renderer->polygon_offsets(renderer->context, center_x, center_y, asteroid->off_x, asteroid->off_y, count,
                                  asteroid_color_table[asteroid->size], asteroid->draw_cache,
                                  &asteroid->draw_cache_valid);
    } else {
        for (vertex = 0; vertex < count; ++vertex) {
            points[vertex * 2] = (int16_t) (center_x + asteroid->off_x[vertex]);
            points[vertex * 2 + 1] = (int16_t) (center_y + asteroid->off_y[vertex]);
        }
        draw_outline(renderer, points, count, asteroid_color_table[asteroid->size]);
    }

    mark_rect(renderer, center_x + asteroid->bound_x0, center_y + asteroid->bound_y0,
              center_x + asteroid->bound_x1, center_y + asteroid->bound_y1);
}

/* ---- stars, power-ups, shield ---- */

/*
 * Stars stay on both screen buffers between frames, so they are not redrawn every frame (a pixel plot is
 * surprisingly slow on the 68000). A star is drawn when its screen is redrawn from scratch, on the two
 * frames after it moves, and otherwise once every eight frames (two in a row, for the two buffers) so that
 * any a rock has wiped out come back quickly.
 */
static void draw_stars(GameState *state, const GameRenderer *renderer) {
    static const int per_layer = GAME_STAR_COUNT / STAR_LAYERS;
    const int phase = (state->frame >> 1) & 3;
    const int everything = state->screen_refresh > 0;
    int16_t plot[STAR_LAYERS][GAME_STAR_COUNT * 2 / STAR_LAYERS];
    int counts[STAR_LAYERS];
    int index;
    int layer;

    for (layer = 0; layer < STAR_LAYERS; ++layer) {
        counts[layer] = 0;
    }
    for (index = 0; index < GAME_STAR_COUNT; ++index) {
        GameStar *star = &state->stars[index];

        /* pixels the stars just left are erased on the next two frames (once per screen buffer) */
        if (star->stale_frames > 0) {
            mark_rect(renderer, star->stale_x, star->stale_y, star->stale_x, star->stale_y);
            --star->stale_frames;
        }
        if (everything || star->fresh_frames > 0 || (index & 3) == phase) {
            if (star->fresh_frames > 0) {
                --star->fresh_frames;
            }
            plot[star->layer][counts[star->layer] * 2] = state->star_points[index * 2];
            plot[star->layer][counts[star->layer] * 2 + 1] = state->star_points[index * 2 + 1];
            ++counts[star->layer];
        }
    }
    if (renderer->points != NULL) {
        for (layer = 0; layer < STAR_LAYERS; ++layer) {
            if (counts[layer] > 0) {
                renderer->points(renderer->context, plot[layer], counts[layer], star_color_table[layer]);
            }
        }
    }
    (void) per_layer;
}

static void draw_powerup(const GameState *state, const GamePowerUp *powerup, const GameRenderer *renderer) {
    const int cx = screen_x(state, powerup->x);
    const int cy = screen_y(state, powerup->y);
    const uint8_t color = powerup_color_table[powerup->type];
    int16_t points[16];
    int index;

    if (powerup->life < 60 && ((powerup->life >> 2) & 1)) {
        return;   /* blinks when about to vanish */
    }
    for (index = 0; index < 8; ++index) {
        points[index * 2] = (int16_t) (cx + scale_x(state, octagon[index][0]));
        points[index * 2 + 1] = (int16_t) (cy + scale_y(state, octagon[index][1]));
    }
    draw_outline(renderer, points, 8, color);

    /* a symbol inside tells the types apart */
    switch (powerup->type) {
    case GAME_POWERUP_SHIELD: {       /* a square */
        int16_t box[8];
        static const int8_t corner[4][2] = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}};
        for (index = 0; index < 4; ++index) {
            box[index * 2] = (int16_t) (cx + scale_x(state, corner[index][0]));
            box[index * 2 + 1] = (int16_t) (cy + scale_y(state, corner[index][1]));
        }
        draw_outline(renderer, box, 4, color);
        break;
    }
    case GAME_POWERUP_RAPID_FIRE:     /* two bars */
        renderer->line(renderer->context, cx + scale_x(state, -2), cy + scale_y(state, -4),
                       cx + scale_x(state, -2), cy + scale_y(state, 4), color);
        renderer->line(renderer->context, cx + scale_x(state, 2), cy + scale_y(state, -4),
                       cx + scale_x(state, 2), cy + scale_y(state, 4), color);
        break;
    case GAME_POWERUP_EXTRA_LIFE:     /* a plus */
        renderer->line(renderer->context, cx + scale_x(state, -4), cy, cx + scale_x(state, 4), cy, color);
        renderer->line(renderer->context, cx, cy + scale_y(state, -4), cx, cy + scale_y(state, 4), color);
        break;
    default:                          /* a cross: double score */
        renderer->line(renderer->context, cx + scale_x(state, -3), cy + scale_y(state, -3),
                       cx + scale_x(state, 3), cy + scale_y(state, 3), color);
        renderer->line(renderer->context, cx + scale_x(state, -3), cy + scale_y(state, 3),
                       cx + scale_x(state, 3), cy + scale_y(state, -3), color);
        break;
    }
    mark_rect(renderer, cx + scale_x(state, -7), cy + scale_y(state, -7), cx + scale_x(state, 7),
              cy + scale_y(state, 7));
}

/* ---- enemies ---- */

static const int8_t ufo_body[6][2] = {{-8, 0}, {-4, -3}, {4, -3}, {8, 0}, {4, 3}, {-4, 3}};
static const int8_t ufo_dome[4][2] = {{-3, -3}, {-2, -6}, {2, -6}, {3, -3}};
static const int8_t brown_body[6][2] = {{-5, 0}, {-3, -4}, {3, -4}, {5, 0}, {3, 4}, {-3, 4}};
static const int8_t green_body[6][2] = {{0, -6}, {4, -2}, {4, 3}, {0, 6}, {-4, 3}, {-4, -2}};
static const int8_t blue_body[4][2] = {{0, -6}, {6, 0}, {0, 6}, {-6, 0}};
static const int8_t purple_body[4][2] = {{0, -3}, {3, 0}, {0, 3}, {-3, 0}};

/* A polygon from a table of world-space offsets around (cx, cy); shift 1 halves the size. */
static void draw_shape(const GameState *state, const GameRenderer *renderer, int cx, int cy,
                       const int8_t (*table)[2], int count, uint8_t color, int shift) {
    int16_t points[16];
    int index;

    for (index = 0; index < count; ++index) {
        points[index * 2] = (int16_t) (cx + scale_x(state, table[index][0] >> shift));
        points[index * 2 + 1] = (int16_t) (cy + scale_y(state, table[index][1] >> shift));
    }
    draw_outline(renderer, points, count, color);
}

static void draw_offset_line(const GameState *state, const GameRenderer *renderer, int cx, int cy,
                             int x0, int y0, int x1, int y1, uint8_t color) {
    renderer->line(renderer->context, cx + scale_x(state, x0), cy + scale_y(state, y0),
                   cx + scale_x(state, x1), cy + scale_y(state, y1), color);
}

static void draw_enemy(const GameState *state, const GameEnemy *enemy, const GameRenderer *renderer) {
    const int cx = screen_x(state, enemy->x);
    const int cy = screen_y(state, enemy->y);
    const uint8_t color = enemy_color_table[enemy->kind];

    if (enemy->flags & 1) {
        return;   /* invisible */
    }
    switch (enemy->kind) {
    case ENEMY_UFO_LARGE:
    case ENEMY_UFO_SMALL: {
        const int shift = (enemy->kind == ENEMY_UFO_SMALL) ? 1 : 0;
        draw_shape(state, renderer, cx, cy, ufo_body, 6, color, shift);
        draw_shape(state, renderer, cx, cy, ufo_dome, 4, color, shift);
        break;
    }
    case ENEMY_BROWN:
        draw_shape(state, renderer, cx, cy, brown_body, 6, color, 0);
        draw_offset_line(state, renderer, cx, cy, -2, -1, -2, 1, GAME_COLOR_WHITE);
        draw_offset_line(state, renderer, cx, cy, 2, -1, 2, 1, GAME_COLOR_WHITE);
        break;
    case ENEMY_GREEN:
        draw_shape(state, renderer, cx, cy, green_body, 6, color, 0);
        draw_offset_line(state, renderer, cx, cy, -2, -2, -1, -2, GAME_COLOR_WHITE);
        draw_offset_line(state, renderer, cx, cy, 1, -2, 2, -2, GAME_COLOR_WHITE);
        break;
    case ENEMY_BLUE:
        if ((enemy->flags & 2) && (enemy->age & 2)) {
            return;   /* flickers while charging a teleport */
        }
        draw_shape(state, renderer, cx, cy, blue_body, 4, color, 0);
        draw_offset_line(state, renderer, cx, cy, -3, 0, 3, 0, color);
        draw_offset_line(state, renderer, cx, cy, 0, -3, 0, 3, color);
        break;
    default:
        draw_shape(state, renderer, cx, cy, purple_body, 4, color, 0);
        break;
    }
    mark_rect(renderer, cx + scale_x(state, -9), cy + scale_y(state, -7), cx + scale_x(state, 9), cy + scale_y(state, 7));
}

static void draw_enemy_bullets(const GameState *state, const GameRenderer *renderer) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        const GameBullet *bullet = &state->enemy_bullets[index];
        int x;
        int y;

        if (!bullet->active) {
            continue;
        }
        x = screen_x(state, bullet->x);
        y = screen_y(state, bullet->y);
        renderer->line(renderer->context, x, y, x + 1, y, bullet->color);
        renderer->line(renderer->context, x, y + 1, x + 1, y + 1, bullet->color);
        mark_rect(renderer, x, y, x + 1, y + 1);
    }
}

/* ---- bosses ---- */

/* An ellipse of the given radii, centred at (cx, cy), as 16 points; `dome` keeps only the upper half. */
static int ellipse_points(const GameState *state, int cx, int cy, int rx, int ry, int count, int dome, int16_t *points) {
    int index;
    int used = 0;

    for (index = 0; index < count; ++index) {
        const uint16_t angle = (uint16_t) (dome ? 32768u + (uint32_t) index * 32768u / (uint32_t) (count - 1) : (uint32_t) index * 65536u / (uint32_t) count);
        const int ox = (int) (mul16((int16_t) rx, (int16_t) trig_cos(angle)) >> 14);
        const int oy = (int) (mul16((int16_t) ry, (int16_t) trig_sin(angle)) >> 14);

        points[used * 2] = (int16_t) (cx + scale_x(state, ox));
        points[used * 2 + 1] = (int16_t) (cy + scale_y(state, oy));
        ++used;
    }
    return used;
}

static void draw_boss(const GameState *state, const GameBoss *boss, const GameRenderer *renderer) {
    const int cx = screen_x(state, boss->x);
    const int cy = screen_y(state, boss->y);
    const uint8_t body = boss->flash > 0 ? GAME_COLOR_WHITE : boss_color_table[boss->kind];
    int16_t points[40];
    int count;
    int index;
    int reach = 24;

    switch (boss->kind) {
    case BOSS_AMIGA_BALL: {
        int meridian;

        count = ellipse_points(state, cx, cy, 20, 20, 16, 0, points);
        draw_outline(renderer, points, count, body);
        for (meridian = 0; meridian < 2; ++meridian) {
            const uint16_t alpha = (uint16_t) (boss->angle + meridian * 21845u);
            const int rx = (int) (mul16(20, (int16_t) trig_cos(alpha)) >> 14);

            count = ellipse_points(state, cx, cy, rx < 0 ? -rx : rx, 20, 12, 0, points);
            draw_outline(renderer, points, count, GAME_COLOR_WHITE);
        }
        draw_offset_line(state, renderer, cx, cy, -20, 0, 20, 0, GAME_COLOR_WHITE);
        reach = 22;
        break;
    }
    case BOSS_FLYING_SAUCER:
        count = ellipse_points(state, cx, cy, 22, 6, 16, 0, points);
        draw_outline(renderer, points, count, body);
        count = ellipse_points(state, cx, cy - scale_y(state, 5), 9, 7, 8, 1, points);
        draw_outline(renderer, points, count, GAME_COLOR_ASTEROID_MEDIUM);
        for (index = 0; index < 4; ++index) {   /* running lights */
            const int lx = -14 + index * 9;
            const uint8_t light = (((boss->phase >> 11) + index) & 1) ? GAME_COLOR_YELLOW : GAME_COLOR_RED;
            draw_offset_line(state, renderer, cx, cy, lx, 1, lx + 1, 1, light);
        }
        reach = 24;
        break;
    case BOSS_TENTACLE:
        count = ellipse_points(state, cx, cy, 14, 14, 12, 0, points);
        draw_outline(renderer, points, count, body);
        for (index = 0; index < 6; ++index) {
            int previous_x = cx;
            int previous_y = cy;
            int segment;

            for (segment = 1; segment <= 4; ++segment) {
                const uint16_t base = (uint16_t) (index * 10923u);
                const int distance = 14 + segment * 6;
                const int wiggle = (int) (mul16(5, (int16_t) trig_sin((uint16_t) (boss->phase * 2 + index * 9000 + segment * 5000))) >> 14);
                const int ox = (int) ((mul16((int16_t) distance, (int16_t) trig_cos(base)) - mul16((int16_t) wiggle, (int16_t) trig_sin(base))) >> 14);
                const int oy = (int) ((mul16((int16_t) distance, (int16_t) trig_sin(base)) + mul16((int16_t) wiggle, (int16_t) trig_cos(base))) >> 14);
                const int x = cx + scale_x(state, ox);
                const int y = cy + scale_y(state, oy);

                renderer->line(renderer->context, previous_x, previous_y, x, y, body);
                previous_x = x;
                previous_y = y;
            }
        }
        reach = 40;
        break;
    default: {   /* Borg cube: a spinning wireframe */
        static const int8_t corner[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                            {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        static const uint8_t edge[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                            {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        int screen[8][2];
        const int32_t cos_y = trig_cos(boss->angle);
        const int32_t sin_y = trig_sin(boss->angle);
        const int32_t cos_x = trig_cos((uint16_t) (boss->angle / 2 + 8192));
        const int32_t sin_x = trig_sin((uint16_t) (boss->angle / 2 + 8192));

        for (index = 0; index < 8; ++index) {
            const int x = corner[index][0] * 14;
            const int y = corner[index][1] * 14;
            const int z = corner[index][2] * 14;
            const int x1 = (int) ((mul16((int16_t) x, (int16_t) cos_y) + mul16((int16_t) z, (int16_t) sin_y)) >> 14);
            const int z1 = (int) ((mul16((int16_t) z, (int16_t) cos_y) - mul16((int16_t) x, (int16_t) sin_y)) >> 14);
            const int y1 = (int) ((mul16((int16_t) y, (int16_t) cos_x) - mul16((int16_t) z1, (int16_t) sin_x)) >> 14);

            screen[index][0] = cx + scale_x(state, x1);
            screen[index][1] = cy + scale_y(state, y1);
        }
        for (index = 0; index < 12; ++index) {
            renderer->line(renderer->context, screen[edge[index][0]][0], screen[edge[index][0]][1],
                           screen[edge[index][1]][0], screen[edge[index][1]][1], body);
        }
        reach = 24;
        break;
    }
    }
    mark_rect(renderer, cx - scale_x(state, reach), cy - scale_y(state, reach), cx + scale_x(state, reach),
              cy + scale_y(state, reach));

    /* hit points along the top edge of the field */
    {
        const int bar = (int) (((int32_t) boss->hp * BOSS_BAR_WIDTH) / (boss->max_hp > 0 ? boss->max_hp : 1));
        const int x = state->field_x + (state->field_width - BOSS_BAR_WIDTH) / 2;
        const int y = state->field_y + 2;

        renderer->line(renderer->context, x, y, x + (bar > 0 ? bar : 1), y, GAME_COLOR_RED);
        renderer->line(renderer->context, x, y + 1, x + (bar > 0 ? bar : 1), y + 1, GAME_COLOR_RED);
        mark_rect(renderer, x, y, x + BOSS_BAR_WIDTH, y + 1);
    }
}

/* ---- text screens ---- */

/* Decimal digits by repeated subtraction: the 68000 has no 32-bit divide, so value / 10 is a slow library call.
   Shows the low `digits` digits (up to 6) of the value, like value % 10^digits would. */
static void format_number(char *out, uint32_t value, int digits) {
    static const uint32_t powers[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    int index;

    if (digits > 6) {
        digits = 6;
    }
    if (value >= powers[digits]) {
        value %= powers[digits];
    }
    out[digits] = 0;
    for (index = 0; index < digits; ++index) {
        const uint32_t power = powers[digits - 1 - index];
        char digit = '0';

        while (value >= power) {
            value -= power;
            ++digit;
        }
        out[index] = digit;
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

/* The HUD is drawn field by field, so a changing hyperspace percentage or power-up timer does not redraw
   the whole strip (text is expensive on the 68000). */
enum {
    HUD_SCORE,
    HUD_HIGH,
    HUD_WAVE,
    HUD_LIVES,
    HUD_HYPER,
    HUD_POWER,
    HUD_FIELDS = HUD_POWER + 3
};

static void draw_hud_field(const GameState *state, const GameRenderer *renderer, int field, int hyper_full) {
    char text[16];
    char icons[10];
    int index;
    int count;

    switch (field) {
    case HUD_SCORE:
        memcpy(text, "SCORE ", 6);
        format_number(text + 6, state->score % 1000000u, 6);
        put_text(renderer, 8, 0, text, GAME_COLOR_YELLOW, GAME_COLOR_FRAME, 1);
        break;
    case HUD_HIGH: {
        const uint32_t high = (state->high_scores[0].score > state->score) ? state->high_scores[0].score : state->score;

        memcpy(text, "HI ", 3);
        format_number(text + 3, high % 1000000u, 6);
        put_text(renderer, 120, 0, text, GAME_COLOR_WHITE, GAME_COLOR_FRAME, 1);
        break;
    }
    case HUD_WAVE:
        memcpy(text, "WAVE ", 5);
        format_number(text + 5, state->wave % 100u, 2);
        put_text(renderer, 240, 0, text, GAME_COLOR_WHITE, GAME_COLOR_FRAME, 1);
        break;
    case HUD_LIVES:
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
        break;
    case HUD_HYPER:
        count = hyperspace_percent(state);
        if (count >= 100) {
            put_text(renderer, 120, 8, "HYPER READY ", GAME_COLOR_SHIP, GAME_COLOR_FRAME, 1);
        } else if (hyper_full) {
            memcpy(text, "HYPER ", 6);
            format_number(text + 6, (uint32_t) count, 2);
            text[8] = '%';
            text[9] = ' ';
            text[10] = ' ';
            text[11] = 0;
            put_text(renderer, 120, 8, text, GAME_COLOR_GREY, GAME_COLOR_FRAME, 1);
        } else {
            /* only the digits change while recharging: the label is already there */
            format_number(text, (uint32_t) count, 2);
            text[2] = '%';
            text[3] = ' ';
            text[4] = ' ';
            text[5] = 0;
            put_text(renderer, 168, 8, text, GAME_COLOR_GREY, GAME_COLOR_FRAME, 1);
        }
        break;
    default: {
        static const char letters[3] = {'S', 'R', 'X'};
        static const uint8_t colors[3] = {GAME_COLOR_SHIP, GAME_COLOR_RED, GAME_COLOR_YELLOW};
        char badge[4];

        index = field - HUD_POWER;
        if (state->hud_powers[index] > 0) {
            badge[0] = letters[index];
            format_number(badge + 1, state->hud_powers[index] % 100u, 2);
        } else {
            memcpy(badge, "   ", 4);
        }
        put_text(renderer, 224 + index * 32, 8, badge, colors[index], GAME_COLOR_FRAME, 1);
        break;
    }
    }
}

static uint8_t power_seconds(uint16_t frames) {
    return (uint8_t) (mul16((int16_t) (frames + 49), 1311) >> 16);   /* frames / 50, rounded up */
}

static void update_hud(GameState *state, const GameRenderer *renderer) {
    const uint32_t high = (state->high_scores[0].score > state->score) ? state->high_scores[0].score : state->score;
    const uint8_t percent = hyperspace_percent(state);
    const uint8_t seconds[3] = {power_seconds(state->shield_timer), power_seconds(state->rapid_timer),
                                power_seconds(state->multiplier_timer)};
    int field;

    /* a changed field is drawn on this frame and the next, to fill both screen buffers */
    if (state->hud_score != state->score) {
        state->hud_score = state->score;
        state->hud_field[HUD_SCORE] = 2;
    }
    if (state->hud_high != high) {
        state->hud_high = high;
        state->hud_field[HUD_HIGH] = 2;
    }
    if (state->hud_wave != state->wave) {
        state->hud_wave = state->wave;
        state->hud_field[HUD_WAVE] = 2;
    }
    if (state->hud_lives != state->lives) {
        state->hud_lives = state->lives;
        state->hud_field[HUD_LIVES] = 2;
    }
    if (state->hud_hyperspace != percent) {
        if ((state->hud_hyperspace >= 100) != (percent >= 100)) {
            state->hud_hyper_full = 2;   /* READY <-> charging changes the label too */
        }
        state->hud_hyperspace = percent;
        state->hud_field[HUD_HYPER] = 2;
    }
    for (field = 0; field < 3; ++field) {
        if (state->hud_powers[field] != seconds[field]) {
            state->hud_powers[field] = seconds[field];
            state->hud_field[HUD_POWER + field] = 2;
        }
    }

    for (field = 0; field < HUD_FIELDS; ++field) {
        if (state->hud_refresh > 0 || state->hud_field[field] > 0) {
            draw_hud_field(state, renderer, field, state->hud_refresh > 0 || state->hud_hyper_full > 0);
            if (state->hud_field[field] > 0) {
                --state->hud_field[field];
            }
        }
    }
    if (state->hud_hyper_full > 0) {
        --state->hud_hyper_full;
    }
    if (state->hud_refresh > 0) {
        --state->hud_refresh;
    }
}

void game_render(GameState *state, const GameRenderer *renderer) {
    int index;

    if (state->screen_refresh > 0 && renderer->clear_field != NULL) {
        renderer->clear_field(renderer->context);
    }

    if (state->mode == GAME_MODE_PLAYING) {
        draw_stars(state, renderer);
        draw_ship(state, renderer);
        draw_bullets(state, renderer);
        if (renderer->rocks != NULL) {
            renderer->rocks(renderer->context, state);
        }
        for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
            if (state->asteroids[index].active && (renderer->rocks == NULL || state->asteroids[index].render_pending)) {
                draw_asteroid(state, &state->asteroids[index], renderer);
            }
        }

        for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
            if (state->powerups[index].active) {
                draw_powerup(state, &state->powerups[index], renderer);
            }
        }
        for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
            if (state->enemies[index].active) {
                draw_enemy(state, &state->enemies[index], renderer);
            }
        }
        draw_enemy_bullets(state, renderer);
        if (state->boss.active) {
            draw_boss(state, &state->boss, renderer);
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
