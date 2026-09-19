/*
 * Keyboard diagnostic for the ST: installs the game's IKBD handler and, for about 25 seconds,
 * logs every change in the held-key state (raw scan codes and the game's input flags) to
 * C:\KEYTEST.LOG. Run in Hatari, press keys, then read the log: make test-keys-atari.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

extern volatile unsigned char st_key_state[128];

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'K', 'E', 'Y', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");
    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

int main(void) {
    PlatformConfig config;
    GameInput input;
    char previous[160];
    char text[160];
    int frame;

    config.resolution = PLATFORM_RES_LOW;
    config.width = 320;
    config.height = 200;
    if (!platform_init(&config)) {
        return 1;
    }

    log_line("ready");
    previous[0] = 0;
    for (frame = 0; frame < 1250; ++frame) {
        int index;
        int length = 0;

        platform_poll_input(&input);
        length += sprintf(text + length, "keys:");
        for (index = 0; index < 128; ++index) {
            if (st_key_state[index]) {
                length += sprintf(text + length, " %02x", index);
            }
        }
        length += sprintf(text + length, " | L%d R%d T%d F%d Q%d M%d", input.left, input.right, input.thrust,
                          input.fire, input.exit_requested, input.toggle_resolution);
        if (strcmp(text, previous) != 0) {
            char stamped[200];
            sprintf(stamped, "f%d %s", frame, text);
            log_line(stamped);
            strcpy(previous, text);
        }
        platform_begin_frame();
        platform_end_frame();
    }

    platform_shutdown();
    log_line("done");
    return 0;
}
