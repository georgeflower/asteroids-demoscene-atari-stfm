/*
 * The frame pacing (run in Hatari: make test-pace-atari). Frames of a chosen length are "worked" with a busy
 * wait, and the number of vertical blanks each took is counted. Results go to C:\PACETEST.LOG.
 *  - steady long frames get a steady cadence of two blanks
 *  - frames near the 20 ms limit (15 and 25 ms alternating) are steady with the lock, and flap without it
 *  - after easy frames the cadence relaxes back to one blank per frame, but not at once
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define ST_FRCLOCK (*(volatile uint32_t *) 0x466UL)

static int failures;
static int checks;

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'P', 'A', 'C', 'E', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static void check(int ok, const char *what) {
    ++checks;
    if (!ok) {
        ++failures;
        log_line(what);
    }
}

/* Run `frames` frames whose work takes work_ms[frame % pattern] ms; count the frames by blanks taken. */
static void run(const int *work_ms, int pattern, int frames, int *histogram) {
    uint32_t last = ST_FRCLOCK;
    int frame;

    for (frame = 0; frame < frames; ++frame) {
        const uint32_t start = ST_HZ200;
        uint32_t taken;

        platform_begin_frame();
        while ((ST_HZ200 - start) * 5u < (uint32_t) work_ms[frame % pattern]) {
        }
        platform_end_frame();
        taken = ST_FRCLOCK - last;
        last = ST_FRCLOCK;
        ++histogram[taken > 4 ? 4 : taken];
    }
}

int main(void) {
    static const int steady_30[1] = {30};
    static const int near_limit[2] = {15, 25};
    static const int easy_10[1] = {10};
    int histogram[5];
    char text[100];

    if (!platform_init()) {
        return 1;
    }

    /* 1: constant 30 ms frames */
    platform_set_pace_lock(1);
    memset(histogram, 0, sizeof(histogram));
    run(steady_30, 1, 40, histogram);
    sprintf(text, "30 ms frames, locked: 1,2,3,4+ blanks = %d %d %d %d", histogram[1], histogram[2], histogram[3], histogram[4]);
    log_line(text);
    check(histogram[2] >= 36 && histogram[3] == 0, "30 ms frames should settle at two blanks");

    /* 2: alternating 15 / 25 ms, locked, then free */
    platform_set_pace_lock(1);
    memset(histogram, 0, sizeof(histogram));
    run(near_limit, 2, 60, histogram);
    sprintf(text, "15/25 ms, locked: %d %d %d %d", histogram[1], histogram[2], histogram[3], histogram[4]);
    log_line(text);
    check(histogram[2] >= 52 && histogram[1] <= 8, "15/25 ms frames should be steady at two blanks when locked");

    platform_set_pace_lock(0);
    memset(histogram, 0, sizeof(histogram));
    run(near_limit, 2, 60, histogram);
    sprintf(text, "15/25 ms, free: %d %d %d %d", histogram[1], histogram[2], histogram[3], histogram[4]);
    log_line(text);
    check(histogram[1] >= 20 && histogram[2] >= 20, "15/25 ms frames should flap between one and two blanks when free");

    /* 3: the lock relaxes after easy frames, but only after a while */
    platform_set_pace_lock(1);
    memset(histogram, 0, sizeof(histogram));
    run(steady_30, 1, 20, histogram);
    memset(histogram, 0, sizeof(histogram));
    run(easy_10, 1, 30, histogram);
    sprintf(text, "10 ms frames right after heavy ones: %d %d %d %d", histogram[1], histogram[2], histogram[3], histogram[4]);
    log_line(text);
    check(histogram[2] >= 20, "the cadence must not drop back at once");
    memset(histogram, 0, sizeof(histogram));
    run(easy_10, 1, 120, histogram);
    sprintf(text, "then 120 more: %d %d %d %d", histogram[1], histogram[2], histogram[3], histogram[4]);
    log_line(text);
    check(histogram[1] >= 40, "the cadence should relax to one blank per frame");

    sprintf(text, "%d checks, %d failures", checks, failures);
    log_line(text);
    platform_shutdown();
    return failures != 0;
}
