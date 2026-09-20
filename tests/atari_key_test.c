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
extern volatile unsigned char st_joystick[2];

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
    GameInput input;
    char previous[160];
    char text[160];
    int frame;

    if (!platform_init()) {
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
        length += sprintf(text + length, " | L%d R%d T%d D%d F%d A%d H%d S%d P%d E%d joy %02x %02x typed %c%s",
                          input.left, input.right, input.thrust, input.down, input.fire, input.fire_alt,
                          input.hyperspace, input.start, input.pause, input.escape, st_joystick[0], st_joystick[1],
                          input.typed ? input.typed : '-', input.backspace ? " BS" : "");
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
