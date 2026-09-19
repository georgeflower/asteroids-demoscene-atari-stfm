#include "game.h"

#include <stddef.h>
#include <string.h>

#define SHIP_TURN_STEP 1
#define SHIP_THRUST 10
#define SHIP_BULLET_SPEED (5 * GAME_FIX_ONE)
#define SHIP_COOLDOWN_FRAMES 8
#define SHIP_INVULNERABILITY_FRAMES 45
#define BULLET_LIFE_FRAMES 45
#define SHIP_RADIUS 8
#define ASTEROID_SPEED_BASE 96

static const int8_t direction_x[GAME_ANGLE_STEPS] = {
    32, 31, 30, 27, 23, 18, 12, 6,
    0, -6, -12, -18, -23, -27, -30, -31,
    -32, -31, -30, -27, -23, -18, -12, -6,
    0, 6, 12, 18, 23, 27, 30, 31
};

static const int8_t direction_y[GAME_ANGLE_STEPS] = {
    0, -6, -12, -18, -23, -27, -30, -31,
    -32, -31, -30, -27, -23, -18, -12, -6,
    0, 6, 12, 18, 23, 27, 30, 31,
    32, 31, 30, 27, 23, 18, 12, 6
};

static uint32_t game_next_random(GameState *state) {
    state->rng_state = state->rng_state * 1103515245u + 12345u;
    return state->rng_state;
}

static int asteroid_radius(const GameAsteroid *asteroid) {
    static const uint8_t radii[] = {0, 12, 20, 30};
    return radii[asteroid->size];
}

static int clamp_to_screen(int value, int limit) {
    if (value < 0) {
        return 0;
    }
    if (value >= limit) {
        return limit - 1;
    }
    return value;
}

static void wrap_position(const GameState *state, int32_t *x, int32_t *y) {
    const int32_t max_x = (int32_t) state->width << GAME_FIX_SHIFT;
    const int32_t max_y = (int32_t) state->height << GAME_FIX_SHIFT;

    while (*x < 0) {
        *x += max_x;
    }
    while (*x >= max_x) {
        *x -= max_x;
    }
    while (*y < 0) {
        *y += max_y;
    }
    while (*y >= max_y) {
        *y -= max_y;
    }
}

static void reset_ship(GameState *state) {
    state->ship.x = ((int32_t) state->width / 2) << GAME_FIX_SHIFT;
    state->ship.y = ((int32_t) state->height / 2) << GAME_FIX_SHIFT;
    state->ship.vx = 0;
    state->ship.vy = 0;
    state->ship.angle = 0;
    state->ship.cooldown = 0;
    state->ship.invulnerability = SHIP_INVULNERABILITY_FRAMES;
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

static void spawn_asteroid(GameState *state, uint8_t size, int32_t x, int32_t y, uint32_t variation) {
    GameAsteroid *asteroid;
    int slot = find_free_asteroid(state);
    int32_t speed_x;
    int32_t speed_y;

    if (slot < 0) {
        return;
    }

    asteroid = &state->asteroids[slot];
    asteroid->active = 1;
    asteroid->size = size;
    asteroid->seed = (uint8_t) (variation & 0xffu);
    asteroid->spin = (uint8_t) ((((variation >> 8) & 3u) + 1u) & 31u);
    asteroid->angle = (uint8_t) ((variation >> 16) & (GAME_ANGLE_STEPS - 1));
    asteroid->x = x;
    asteroid->y = y;

    speed_x = ((int32_t) (((variation >> 20) & 7u) + 2) * ASTEROID_SPEED_BASE);
    speed_y = ((int32_t) (((variation >> 24) & 7u) + 2) * ASTEROID_SPEED_BASE);

    asteroid->vx = (variation & 0x10000u) ? speed_x : -speed_x;
    asteroid->vy = (variation & 0x20000u) ? speed_y : -speed_y;
}

static void spawn_wave(GameState *state) {
    uint8_t count = (uint8_t) (3 + state->wave);
    uint8_t i;

    if (count > 7) {
        count = 7;
    }

    for (i = 0; i < count; ++i) {
        const uint32_t variation = game_next_random(state);
        const int32_t x = ((variation & 1u) ? 12 : (int32_t) state->width - 12) << GAME_FIX_SHIFT;
        const int32_t y = ((int32_t) (20 + ((variation >> 8) % (state->height - 40)))) << GAME_FIX_SHIFT;
        spawn_asteroid(state, 3, x, y, variation);
    }
}

static void split_asteroid(GameState *state, const GameAsteroid *asteroid) {
    if (asteroid->size <= 1) {
        return;
    }

    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y, game_next_random(state));
    spawn_asteroid(state, (uint8_t) (asteroid->size - 1), asteroid->x, asteroid->y, game_next_random(state));
}

static uint8_t any_manual_input(const GameInput *input) {
    return (uint8_t) (input->left || input->right || input->thrust || input->fire);
}

static void select_demo_input(const GameState *state, GameInput *ai) {
    const GameAsteroid *target = NULL;
    uint32_t best_distance = 0xffffffffu;
    int index;

    memset(ai, 0, sizeof(*ai));

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        const GameAsteroid *asteroid = &state->asteroids[index];
        if (asteroid->active) {
            const int32_t dx = (asteroid->x - state->ship.x) >> GAME_FIX_SHIFT;
            const int32_t dy = (asteroid->y - state->ship.y) >> GAME_FIX_SHIFT;
            const uint32_t distance = (uint32_t) ((dx * dx) + (dy * dy));
            if (distance < best_distance) {
                best_distance = distance;
                target = asteroid;
            }
        }
    }

    if (target != NULL) {
        int best_angle = 0;
        int best_dot = -2147483647;
        int step;
        for (step = 0; step < GAME_ANGLE_STEPS; ++step) {
            const int dot = direction_x[step] * (int) ((target->x - state->ship.x) >> GAME_FIX_SHIFT)
                + direction_y[step] * (int) ((target->y - state->ship.y) >> GAME_FIX_SHIFT);
            if (dot > best_dot) {
                best_dot = dot;
                best_angle = step;
            }
        }

        if (((state->ship.angle + GAME_ANGLE_STEPS) - best_angle) % GAME_ANGLE_STEPS > GAME_ANGLE_STEPS / 2) {
            ai->right = 1;
        } else if (state->ship.angle != best_angle) {
            ai->left = 1;
        }

        if (best_distance > (uint32_t) (48 * 48)) {
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
            bullet->vx = state->ship.vx + ((int32_t) direction_x[state->ship.angle] * SHIP_BULLET_SPEED) / 32;
            bullet->vy = state->ship.vy + ((int32_t) direction_y[state->ship.angle] * SHIP_BULLET_SPEED) / 32;
            state->ship.cooldown = SHIP_COOLDOWN_FRAMES;
            return;
        }
    }
}

static void update_ship(GameState *state, const GameInput *input) {
    if (input->left) {
        state->ship.angle = (uint8_t) ((state->ship.angle + GAME_ANGLE_STEPS - SHIP_TURN_STEP) % GAME_ANGLE_STEPS);
    }
    if (input->right) {
        state->ship.angle = (uint8_t) ((state->ship.angle + SHIP_TURN_STEP) % GAME_ANGLE_STEPS);
    }
    if (input->thrust) {
        state->ship.vx += ((int32_t) direction_x[state->ship.angle] * SHIP_THRUST);
        state->ship.vy += ((int32_t) direction_y[state->ship.angle] * SHIP_THRUST);
    }

    state->ship.x += state->ship.vx;
    state->ship.y += state->ship.vy;
    state->ship.vx -= state->ship.vx / 64;
    state->ship.vy -= state->ship.vy / 64;
    wrap_position(state, &state->ship.x, &state->ship.y);

    if (state->ship.cooldown > 0) {
        --state->ship.cooldown;
    }
    if (state->ship.invulnerability > 0) {
        --state->ship.invulnerability;
    }
    if (input->fire && state->ship.cooldown == 0) {
        fire_bullet(state);
    }
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
        wrap_position(state, &bullet->x, &bullet->y);
        if (bullet->life > 0) {
            --bullet->life;
        }
        if (bullet->life == 0) {
            bullet->active = 0;
        }
    }
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
        asteroid->angle = (uint8_t) ((asteroid->angle + asteroid->spin) % GAME_ANGLE_STEPS);
        wrap_position(state, &asteroid->x, &asteroid->y);
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
            const int radius = asteroid_radius(asteroid);
            const int32_t dx = (bullet->x - asteroid->x) >> GAME_FIX_SHIFT;
            const int32_t dy = (bullet->y - asteroid->y) >> GAME_FIX_SHIFT;
            if (!asteroid->active) {
                continue;
            }
            if ((dx * dx) + (dy * dy) <= (radius * radius)) {
                GameAsteroid exploded = *asteroid;
                bullet->active = 0;
                asteroid->active = 0;
                state->score = (uint16_t) (state->score + (uint16_t) (exploded.size * 10));
                split_asteroid(state, &exploded);
                break;
            }
        }
    }
}

static void resolve_ship_collisions(GameState *state) {
    int asteroid_index;

    if (state->ship.invulnerability > 0) {
        return;
    }

    for (asteroid_index = 0; asteroid_index < GAME_MAX_ASTEROIDS; ++asteroid_index) {
        GameAsteroid *asteroid = &state->asteroids[asteroid_index];
        const int radius = asteroid_radius(asteroid) + SHIP_RADIUS;
        const int32_t dx = (state->ship.x - asteroid->x) >> GAME_FIX_SHIFT;
        const int32_t dy = (state->ship.y - asteroid->y) >> GAME_FIX_SHIFT;

        if (!asteroid->active) {
            continue;
        }
        if ((dx * dx) + (dy * dy) <= (radius * radius)) {
            if (state->lives > 0) {
                --state->lives;
            }
            reset_ship(state);
            if (state->lives == 0) {
                state->score = 0;
                state->wave = 1;
                memset(state->asteroids, 0, sizeof(state->asteroids));
                memset(state->bullets, 0, sizeof(state->bullets));
                state->lives = 3;
                spawn_wave(state);
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
    spawn_wave(state);
}

void game_set_resolution(GameState *state, uint16_t width, uint16_t height) {
    state->width = width;
    state->height = height;
    reset_ship(state);
}

void game_step(GameState *state, const GameInput *input) {
    GameInput effective_input;
    ++state->frame;

    if (any_manual_input(input)) {
        effective_input = *input;
        state->demo_mode = 0;
    } else {
        select_demo_input(state, &effective_input);
        state->demo_mode = 1;
    }

    update_ship(state, &effective_input);
    update_bullets(state);
    update_asteroids(state);
    resolve_bullet_collisions(state);
    resolve_ship_collisions(state);

    if (!active_asteroids(state)) {
        ++state->wave;
        spawn_wave(state);
    }
}

static void draw_ship(const GameState *state, void *context, GameLineDrawer draw_line) {
    int px = (int) (state->ship.x >> GAME_FIX_SHIFT);
    int py = (int) (state->ship.y >> GAME_FIX_SHIFT);
    int angle = state->ship.angle;
    int tip_x = px + direction_x[angle] / 2;
    int tip_y = py + direction_y[angle] / 2;
    int left_angle = (angle + GAME_ANGLE_STEPS - 11) % GAME_ANGLE_STEPS;
    int right_angle = (angle + 11) % GAME_ANGLE_STEPS;
    int left_x = px - direction_x[left_angle] / 3;
    int left_y = py - direction_y[left_angle] / 3;
    int right_x = px - direction_x[right_angle] / 3;
    int right_y = py - direction_y[right_angle] / 3;

    if (state->ship.invulnerability > 0 && (state->frame & 2) != 0) {
        return;
    }

    draw_line(context, clamp_to_screen(tip_x, state->width), clamp_to_screen(tip_y, state->height), clamp_to_screen(left_x, state->width), clamp_to_screen(left_y, state->height), 1);
    draw_line(context, clamp_to_screen(left_x, state->width), clamp_to_screen(left_y, state->height), clamp_to_screen(right_x, state->width), clamp_to_screen(right_y, state->height), 1);
    draw_line(context, clamp_to_screen(right_x, state->width), clamp_to_screen(right_y, state->height), clamp_to_screen(tip_x, state->width), clamp_to_screen(tip_y, state->height), 1);

    if (!state->demo_mode && (state->frame & 1) == 0) {
        const int rear_x = px - direction_x[angle] / 4;
        const int rear_y = py - direction_y[angle] / 4;
        draw_line(context, clamp_to_screen(rear_x, state->width), clamp_to_screen(rear_y, state->height), clamp_to_screen(left_x, state->width), clamp_to_screen(left_y, state->height), 1);
        draw_line(context, clamp_to_screen(rear_x, state->width), clamp_to_screen(rear_y, state->height), clamp_to_screen(right_x, state->width), clamp_to_screen(right_y, state->height), 1);
    }
}

static void draw_bullets(const GameState *state, void *context, GameLineDrawer draw_line) {
    int index;
    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        const GameBullet *bullet = &state->bullets[index];
        if (!bullet->active) {
            continue;
        }
        const int x = clamp_to_screen((int) (bullet->x >> GAME_FIX_SHIFT), state->width);
        const int y = clamp_to_screen((int) (bullet->y >> GAME_FIX_SHIFT), state->height);
        draw_line(context, x - 1, y, x + 1, y, 1);
        draw_line(context, x, y - 1, x, y + 1, 1);
    }
}

static void draw_asteroid(const GameState *state, const GameAsteroid *asteroid, void *context, GameLineDrawer draw_line) {
    int center_x = (int) (asteroid->x >> GAME_FIX_SHIFT);
    int center_y = (int) (asteroid->y >> GAME_FIX_SHIFT);
    int radius = asteroid_radius(asteroid);
    int first_x = 0;
    int first_y = 0;
    int previous_x = 0;
    int previous_y = 0;
    int vertex;

    for (vertex = 0; vertex < 8; ++vertex) {
        const int angle = (asteroid->angle + vertex * 4) % GAME_ANGLE_STEPS;
        const int wobble = (int) (((asteroid->seed >> (vertex & 3)) & 3u) - 1);
        const int scaled_radius = radius + wobble * 2;
        const int x = clamp_to_screen(center_x + (direction_x[angle] * scaled_radius) / 32, state->width);
        const int y = clamp_to_screen(center_y + (direction_y[angle] * scaled_radius) / 32, state->height);

        if (vertex == 0) {
            first_x = x;
            first_y = y;
        } else {
            draw_line(context, previous_x, previous_y, x, y, 1);
        }
        previous_x = x;
        previous_y = y;
    }

    draw_line(context, previous_x, previous_y, first_x, first_y, 1);
}

void game_render(const GameState *state, void *context, GameLineDrawer draw_line) {
    int index;

    draw_ship(state, context, draw_line);
    draw_bullets(state, context, draw_line);

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state->asteroids[index].active) {
            draw_asteroid(state, &state->asteroids[index], context, draw_line);
        }
    }
}
