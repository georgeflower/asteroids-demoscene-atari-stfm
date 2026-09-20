/*
 * Compiled only to `-S` by the Makefile: the assembly (st_rocks.S) needs the byte offsets of GameState and
 * GameAsteroid fields, and this is how it gets them without writing them out by hand. Every DEF line ends up
 * in the assembler output as `->NAME #value`, which the Makefile turns into `.set NAME, value`.
 */
#include "game.h"

#include <stddef.h>

#define DEF(name, value) __asm__ volatile ("\n->" #name " %0" : : "i" (value))

/* st_rocks.S plots the rock in the bitplane (4 - size) * 2: the three rock colours must be planes 3, 2 and 1 */
typedef char rock_plane_check[(GAME_COLOR_ASTEROID_LARGE == 2 && GAME_COLOR_ASTEROID_MEDIUM == 4 &&
                               GAME_COLOR_ASTEROID_SMALL == 8 && GAME_ASTEROID_SMALL == 1 &&
                               GAME_ASTEROID_MEDIUM == 2 && GAME_ASTEROID_LARGE == 3) ? 1 : -1];

void layout(void) {
    DEF(GS_FIELD_X, offsetof(GameState, field_x));
    DEF(GS_FIELD_Y, offsetof(GameState, field_y));
    DEF(GS_FIELD_W, offsetof(GameState, field_width));
    DEF(GS_FIELD_H, offsetof(GameState, field_height));
    DEF(GS_X_SCALE, offsetof(GameState, x_scale));
    DEF(GS_Y_SCALE, offsetof(GameState, y_scale));
    DEF(GS_ASTEROIDS, offsetof(GameState, asteroids));
    DEF(GA_SIZEOF, sizeof(GameAsteroid));
    DEF(GA_MAX, GAME_MAX_ASTEROIDS);
    DEF(GA_X, offsetof(GameAsteroid, x));
    DEF(GA_Y, offsetof(GameAsteroid, y));
    DEF(GA_ANGLE, offsetof(GameAsteroid, angle));
    DEF(GA_ACTIVE, offsetof(GameAsteroid, active));
    DEF(GA_SIZE, offsetof(GameAsteroid, size));
    DEF(GA_BOUND_X0, offsetof(GameAsteroid, bound_x0));
    DEF(GA_BOUND_Y0, offsetof(GameAsteroid, bound_y0));
    DEF(GA_BOUND_X1, offsetof(GameAsteroid, bound_x1));
    DEF(GA_BOUND_Y1, offsetof(GameAsteroid, bound_y1));
    DEF(GA_CACHE_INDEX, offsetof(GameAsteroid, cache_index));
    DEF(GA_CACHE_VALID, offsetof(GameAsteroid, cache_valid));
    DEF(GA_DRAW_CACHE_VALID, offsetof(GameAsteroid, draw_cache_valid));
    DEF(GA_DRAW_CACHE, offsetof(GameAsteroid, draw_cache));
    DEF(GA_RENDER_PENDING, offsetof(GameAsteroid, render_pending));
    DEF(GA_VX, offsetof(GameAsteroid, vx));
    DEF(GA_VY, offsetof(GameAsteroid, vy));
    DEF(GA_SPIN, offsetof(GameAsteroid, spin));
    DEF(GA_DRAW_COUNT, offsetof(GameAsteroid, draw_count));
    DEF(GA_OFF_X, offsetof(GameAsteroid, off_x));
    DEF(GA_OFF_Y, offsetof(GameAsteroid, off_y));
    DEF(GS_BULLETS, offsetof(GameState, bullets));
    DEF(GB_SIZEOF, sizeof(GameBullet));
    DEF(GB_MAX, GAME_MAX_BULLETS);
    DEF(GB_X, offsetof(GameBullet, x));
    DEF(GB_Y, offsetof(GameBullet, y));
    DEF(GB_VX, offsetof(GameBullet, vx));
    DEF(GB_VY, offsetof(GameBullet, vy));
    DEF(GB_ACTIVE, offsetof(GameBullet, active));
    DEF(GB_LIFE, offsetof(GameBullet, life));
    DEF(WORLD_MAX_X, (long) GAME_WORLD_WIDTH << GAME_FIX_SHIFT);
    DEF(WORLD_MAX_Y, (long) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT);
}
