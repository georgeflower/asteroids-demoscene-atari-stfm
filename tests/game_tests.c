#include "game.h"

#include <assert.h>
#include <string.h>

static void test_initial_wave_spawns_asteroids(void) {
    GameState state;
    int active = 0;
    int index;

    game_init(&state, 320, 200);

    assert(state.width == 320);
    assert(state.height == 200);
    assert(state.wave == 1);
    assert(state.lives == 3);

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state.asteroids[index].active) {
            ++active;
        }
    }

    assert(active >= 1);
}

static void test_bullet_breaks_large_asteroid(void) {
    GameState state;
    int medium_asteroids = 0;
    int index;

    game_init(&state, 320, 200);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    memset(state.bullets, 0, sizeof(state.bullets));

    state.asteroids[0].active = 1;
    state.asteroids[0].size = 3;
    state.asteroids[0].x = 100 << GAME_FIX_SHIFT;
    state.asteroids[0].y = 50 << GAME_FIX_SHIFT;

    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.asteroids[0].x;
    state.bullets[0].y = state.asteroids[0].y;

    game_step(&state, &(GameInput) {0});

    assert(state.score == 30);
    assert(!state.bullets[0].active);
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state.asteroids[index].active && state.asteroids[index].size == 2) {
            ++medium_asteroids;
        }
    }
    assert(medium_asteroids >= 2);
}

static void test_ship_wraps_across_screen(void) {
    GameState state;

    game_init(&state, 320, 200);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ship.x = -1;
    state.ship.y = -1;

    game_step(&state, &(GameInput) {0});

    assert(state.ship.x >= 0);
    assert(state.ship.y >= 0);
}

int main(void) {
    test_initial_wave_spawns_asteroids();
    test_bullet_breaks_large_asteroid();
    test_ship_wraps_across_screen();
    return 0;
}
