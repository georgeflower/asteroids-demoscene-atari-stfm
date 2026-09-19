#include "game.h"
#include "platform.h"

#include <stddef.h>

int main(void) {
    GameState game;
    GameInput input;
    GameRenderer renderer;
    uint8_t score_file[GAME_SCORE_FILE_BYTES];
    int steps;

    renderer.context = NULL;
    renderer.line = platform_draw_line;
    renderer.polygon = platform_draw_polygon;
    renderer.dirty = platform_mark_dirty;
    renderer.text = platform_draw_text;
    renderer.clear_field = platform_clear_field;

    if (!platform_init()) {
        return 1;
    }

    game_init(&game, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    if (platform_load_scores(score_file, (int) sizeof(score_file)) == (int) sizeof(score_file)) {
        (void) game_scores_unpack(&game, score_file);
    }

    for (;;) {
        platform_poll_input(&input);
        if (input.exit_requested) {
            break;
        }

        /* one game step per vertical blank, so the game keeps its speed when a frame takes longer */
        for (steps = platform_take_elapsed_frames(); steps > 0; --steps) {
            game_step(&game, &input);
        }
        if (game.scores_changed) {
            game_scores_pack(&game, score_file);
            platform_save_scores(score_file, (int) sizeof(score_file));
            game.scores_changed = 0;
        }

        platform_begin_frame();
        game_render(&game, &renderer);
        platform_end_frame();
    }

    platform_shutdown();
    return 0;
}
