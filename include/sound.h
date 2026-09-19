#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/*
 * Sound effects for the YM2149 (the ST's sound chip). The engine is portable: it works out what
 * to write to the chip's 14 registers each 50 Hz frame and hands the writes to a callback, so it
 * can be tested without hardware.
 *
 * Three voices: A plays one-shot tonal effects, B plays one-shot noise effects (explosions), and
 * C carries the looping sounds (engine thrust, UFO warble).
 */
enum {
    SFX_NONE = 0,
    SFX_SHOOT,
    SFX_EXPLODE_LARGE,
    SFX_EXPLODE_MEDIUM,
    SFX_EXPLODE_SMALL,
    SFX_SHIP_DEATH,
    SFX_POWERUP,
    SFX_EXTRA_LIFE,
    SFX_HYPERSPACE,
    SFX_ENEMY_SHOT,
    SFX_BOSS_HIT,
    SFX_GAME_OVER,
    SFX_WAVE_START,
    SFX_MENU,
    SFX_COUNT
};

typedef void (*SoundWriter)(uint8_t reg, uint8_t value);

void sound_init(SoundWriter writer);   /* takes over the chip and silences it */
void sound_play(int sfx);
void sound_set_thrust(int on);
void sound_set_ufo(int on);
void sound_tick(void);                 /* call once per 50 Hz frame */
void sound_silence(void);              /* everything off, chip left quiet */

#endif
