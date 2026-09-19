#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#include "game.h"

enum {
    PLATFORM_RES_LOW = 0,
    PLATFORM_RES_MEDIUM = 1
};

typedef struct PlatformConfig {
    uint16_t width;
    uint16_t height;
    uint8_t resolution;
} PlatformConfig;

int platform_init(const PlatformConfig *config);
void platform_shutdown(void);
void platform_poll_input(GameInput *input);
void platform_begin_frame(void);
void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color);
void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color);
void platform_end_frame(void);
void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1);
int platform_take_elapsed_frames(void);
int platform_cycle_resolution(PlatformConfig *config);

#endif
