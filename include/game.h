#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define GAME_MAX_ASTEROIDS 12
#define GAME_MAX_BULLETS 6
#define GAME_ANGLE_STEPS 32
#define GAME_FIX_SHIFT 8
#define GAME_FIX_ONE (1L << GAME_FIX_SHIFT)

typedef struct GameInput {
    uint8_t left;
    uint8_t right;
    uint8_t thrust;
    uint8_t fire;
    uint8_t exit_requested;
    uint8_t toggle_resolution;
} GameInput;

typedef struct GameShip {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t angle;
    uint8_t cooldown;
    uint8_t invulnerability;
} GameShip;

typedef struct GameBullet {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t active;
    uint8_t life;
} GameBullet;

typedef struct GameAsteroid {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t active;
    uint8_t size;
    uint8_t seed;
    uint8_t spin;
    uint8_t angle;
} GameAsteroid;

typedef struct GameState {
    uint16_t width;
    uint16_t height;
    uint16_t score;
    uint16_t frame;
    uint8_t wave;
    uint8_t lives;
    uint8_t demo_mode;
    GameShip ship;
    GameBullet bullets[GAME_MAX_BULLETS];
    GameAsteroid asteroids[GAME_MAX_ASTEROIDS];
    uint32_t rng_state;
} GameState;

typedef void (*GameLineDrawer)(void *context, int x0, int y0, int x1, int y1, uint8_t color);

void game_init(GameState *state, uint16_t width, uint16_t height);
void game_set_resolution(GameState *state, uint16_t width, uint16_t height);
void game_step(GameState *state, const GameInput *input);
void game_render(const GameState *state, void *context, GameLineDrawer draw_line);

#endif
