#include "game.h"
#include "platform.h"

#include <stddef.h>
#include <string.h>

static void select_config(int argc, char **argv, PlatformConfig *config) {
    int index;

    config->resolution = PLATFORM_RES_LOW;
    config->width = 320;
    config->height = 200;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--medium") == 0 || strcmp(argv[index], "medium") == 0 || strcmp(argv[index], "-m") == 0) {
            config->resolution = PLATFORM_RES_MEDIUM;
            config->width = 640;
            config->height = 200;
        }
    }
}

int main(int argc, char **argv) {
    PlatformConfig config;
    GameState game;
    GameInput input;
    int steps;

    select_config(argc, argv, &config);
    if (!platform_init(&config)) {
        return 1;
    }

    game_init(&game, config.width, config.height);

    for (;;) {
        platform_poll_input(&input);
        if (input.exit_requested) {
            break;
        }
        if (input.toggle_resolution) {
            if (!platform_cycle_resolution(&config)) {
                break;
            }
            game_set_resolution(&game, config.width, config.height);
        }

        /* one game step per vertical blank, so the game keeps its speed when a frame takes longer */
        for (steps = platform_take_elapsed_frames(); steps > 0; --steps) {
            game_step(&game, &input);
        }
        platform_begin_frame();
        game_render(&game, NULL, platform_draw_line, platform_draw_polygon, platform_mark_dirty);
        platform_end_frame();
    }

    platform_shutdown();
    return 0;
}
