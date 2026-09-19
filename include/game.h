#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#include "sound.h"

/*
 * The game world is a virtual 320x240 square-pixel playfield (the Lovable
 * 800x600 canvas scaled by 0.4).  game_render() maps it onto the playing field
 * rectangle of the real screen, so the world keeps its proportions on a 4:3 display.
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

#define GAME_HIGH_SCORE_COUNT 5
#define GAME_MIN_SCORE_FOR_INITIALS 1000
#define GAME_EXTRA_LIFE_INTERVAL 10000
#define GAME_START_LIVES 3
#define GAME_HYPERSPACE_RECHARGE_FRAMES 500   /* 10 s at 50 Hz */

/* High score file: "AST1" + 5 entries of (3 initials, 0, score as 4 bytes big endian). */
#define GAME_SCORE_FILE_BYTES 44

/* Palette indices. Line colours (ship, rocks) each sit on a single bitplane
   (1, 2, 4, 8), which the assembly line drawer can plot quickly. */
#define GAME_COLOR_BLACK 0
#define GAME_COLOR_SHIP 1
#define GAME_COLOR_ASTEROID_LARGE 2
#define GAME_COLOR_FLAME 3
#define GAME_COLOR_ASTEROID_MEDIUM 4
#define GAME_COLOR_FRAME 5
#define GAME_COLOR_YELLOW 6
#define GAME_COLOR_RED 7
#define GAME_COLOR_ASTEROID_SMALL 8
#define GAME_COLOR_GREY 9
#define GAME_COLOR_WHITE GAME_COLOR_ASTEROID_LARGE

#define GAME_ASTEROID_SMALL 1
#define GAME_ASTEROID_MEDIUM 2
#define GAME_ASTEROID_LARGE 3

/* Screens. */
enum {
    GAME_MODE_TITLE = 0,
    GAME_MODE_PLAYING,
    GAME_MODE_GAME_OVER,
    GAME_MODE_ENTER_INITIALS
};

typedef struct GameInput {
    uint8_t left;
    uint8_t right;
    uint8_t thrust;
    uint8_t fire;
    uint8_t hyperspace;
    uint8_t start;          /* Space or Return: start a game, confirm */
    uint8_t pause;
    uint8_t exit_requested;
} GameInput;

typedef struct GameShip {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint16_t angle;
    uint16_t hyperspace_cooldown;
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

typedef struct GameHighScore {
    uint32_t score;
    char initials[4];
} GameHighScore;

typedef struct GameState {
    /* the playing field rectangle on the screen, and the world -> screen scales (8.8 fixed point) */
    uint16_t field_x;
    uint16_t field_y;
    uint16_t field_width;
    uint16_t field_height;
    uint16_t x_scale;
    uint16_t y_scale;

    uint8_t mode;
    uint16_t mode_timer;
    uint8_t paused;
    uint16_t banner_timer;      /* frames left of the "WAVE n" banner */
    uint32_t score;
    uint32_t next_extra_life;
    uint16_t frame;
    uint8_t wave;
    uint8_t lives;

    GameShip ship;
    GameBullet bullets[GAME_MAX_BULLETS];
    GameAsteroid asteroids[GAME_MAX_ASTEROIDS];
    uint32_t rng_state;

    GameHighScore high_scores[GAME_HIGH_SCORE_COUNT];
    char entry[4];              /* initials being entered */
    uint8_t entry_position;
    uint8_t repeat_timer;
    uint8_t scores_changed;     /* set when the high score table changed and should be saved */
    uint16_t sound_events;      /* bit per SFX_* id: effects to play (taken by game_take_sound_events) */
    GameInput previous;         /* last step's input, for detecting key presses */

    /* what is currently drawn on the (double-buffered) screen, so it is only redrawn on change */
    uint8_t screen_refresh;     /* frames of the full static screen (title, game over ...) still to draw */
    uint8_t prompt_refresh;
    uint8_t prompt_visible;
    uint8_t hud_refresh;
    uint32_t hud_score;
    uint32_t hud_high;
    uint8_t hud_lives;
    uint8_t hud_wave;
    uint8_t hud_hyperspace;     /* percent charged */
} GameState;

typedef void (*GameLineDrawer)(void *context, int x0, int y0, int x1, int y1, uint8_t color);

/* Optional: draws a closed outline in one call. points holds x,y pairs (screen space, may lie
   off-screen). When absent, game_render() draws the outline with the line drawer. */
typedef void (*GamePolygonDrawer)(void *context, const int16_t *points, int count, uint8_t color);

/* Optional: called with the screen-space bounding box of everything game_render() draws
   on the playing field, so the platform can erase just those areas next time. */
typedef void (*GameDirtyMarker)(void *context, int x0, int y0, int x1, int y1);

/* Optional: opaque bitmap text. x is a multiple of 8 (16 at scale 2), scale is 1 or 2. */
typedef void (*GameTextDrawer)(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale);

/* Optional: clear the whole playing field (for the static screens). */
typedef void (*GameFieldClearer)(void *context);

typedef struct GameRenderer {
    void *context;
    GameLineDrawer line;
    GamePolygonDrawer polygon;
    GameDirtyMarker dirty;
    GameTextDrawer text;
    GameFieldClearer clear_field;
} GameRenderer;

/* The field rectangle is where the world is drawn on the screen. The game starts on the title screen. */
void game_init(GameState *state, uint16_t field_x, uint16_t field_y, uint16_t field_width, uint16_t field_height);
void game_start(GameState *state);   /* begin a new game right away (what pressing start does) */
void game_step(GameState *state, const GameInput *input);
void game_render(GameState *state, const GameRenderer *renderer);

/* Sound effects triggered since the last call, as a bit mask of 1 << SFX_*. */
uint16_t game_take_sound_events(GameState *state);

/* High score table <-> file image. unpack returns 1 if the data was valid. */
void game_scores_pack(const GameState *state, uint8_t *out);
int game_scores_unpack(GameState *state, const uint8_t *data);

#endif
