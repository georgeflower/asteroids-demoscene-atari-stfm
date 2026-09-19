#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/*
 * The game world is a virtual 320x240 square-pixel playfield (the Lovable
 * 800x600 canvas scaled by 0.4).  game_render() maps it onto the real screen,
 * so 320x200 low resolution and 640x200 medium resolution show the same world
 * with correct proportions on a 4:3 display.
 *
 * Positions and velocities are 16.16 fixed point in world units per 50 Hz
 * frame.  Angles are 16-bit: 65536 is one full turn, 0 points right, and
 * increasing angles turn clockwise on screen (y grows downwards).
 */
#define GAME_MAX_ASTEROIDS 40
#define GAME_MAX_BULLETS 6
#define GAME_MAX_ASTEROID_POINTS 11
#define GAME_FIX_SHIFT 16
#define GAME_FIX_ONE (1L << GAME_FIX_SHIFT)
#define GAME_WORLD_WIDTH 320
#define GAME_WORLD_HEIGHT 240

/* Line colours handed to the platform layer. Each is a single bitplane in
   4-plane low resolution, which the assembly line drawer can plot quickly. */
#define GAME_COLOR_SHIP 1
#define GAME_COLOR_FLAME 3
#define GAME_COLOR_ASTEROID_LARGE 2
#define GAME_COLOR_ASTEROID_MEDIUM 4
#define GAME_COLOR_ASTEROID_SMALL 8

#define GAME_ASTEROID_SMALL 1
#define GAME_ASTEROID_MEDIUM 2
#define GAME_ASTEROID_LARGE 3

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
    uint16_t angle;
    uint8_t cooldown;
    uint8_t invulnerability;
    uint8_t thrusting;
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
    uint16_t angle;
    int16_t spin;
    uint8_t active;
    uint8_t size;
    uint8_t point_count;
    uint8_t radius[GAME_MAX_ASTEROID_POINTS];
    /* screen-space vertex offsets for the cached orientation (render cache) */
    int8_t off_x[GAME_MAX_ASTEROID_POINTS];
    int8_t off_y[GAME_MAX_ASTEROID_POINTS];
    int8_t bound_x0;    /* tight bounding box of the cached outline, relative to the centre */
    int8_t bound_y0;
    int8_t bound_x1;
    int8_t bound_y1;
    uint8_t cache_index;
    uint8_t cache_valid;
} GameAsteroid;

typedef struct GameState {
    uint16_t width;     /* screen size in pixels */
    uint16_t height;
    uint16_t x_scale;   /* world -> screen scale, 8.8 fixed point */
    uint16_t y_scale;
    uint32_t score;
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

/* Optional: draws a closed outline in one call. points holds x,y pairs (screen space, may lie
   off-screen). When absent, game_render() draws the outline with the line drawer. */
typedef void (*GamePolygonDrawer)(void *context, const int16_t *points, int count, uint8_t color);

/* Optional: called with the screen-space bounding box of everything game_render() draws,
   so the platform can erase just those areas next time instead of clearing the screen. */
typedef void (*GameDirtyMarker)(void *context, int x0, int y0, int x1, int y1);

void game_init(GameState *state, uint16_t width, uint16_t height);
void game_set_resolution(GameState *state, uint16_t width, uint16_t height);
void game_step(GameState *state, const GameInput *input);
void game_render(GameState *state, void *context, GameLineDrawer draw_line, GamePolygonDrawer draw_polygon,
                 GameDirtyMarker mark_dirty);

#endif
