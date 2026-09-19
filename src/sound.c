#include "sound.h"

#include <stddef.h>

/*
 * YM2149 registers: 0-5 tone periods (A, B, C: fine byte, coarse nibble), 6 noise period,
 * 7 mixer (bits 0-2 switch tone off, 3-5 switch noise off; bits 6 and 7 must stay set on the ST
 * because they make the parallel/floppy-select port lines outputs), 8-10 volumes.
 * Tone period T gives 125000 / T Hz.
 */
#define REG_TONE(voice) ((voice) * 2)
#define REG_NOISE 6
#define REG_MIXER 7
#define REG_VOLUME(voice) (8 + (voice))
#define MIXER_PORTS_OUT 0xc0

#define VOICE_A 0
#define VOICE_B 1
#define VOICE_C 2

/* One effect on one voice. Per frame f: tone = tone + step * f (or the note list), volume falls one step every `decay` frames. */
typedef struct SfxDef {
    uint8_t frames;
    uint16_t tone;            /* period; 0 = no tone */
    int16_t step;             /* period change per frame */
    uint8_t noise;            /* noise period 1-31; 0 = no noise */
    uint8_t volume;           /* 0-15 */
    uint8_t decay;            /* frames per volume step; 0 = constant */
    const uint16_t *notes;    /* optional arpeggio (periods) */
    uint8_t note_frames;
} SfxDef;

static const uint16_t notes_powerup[] = {239, 190, 159, 119};
static const uint16_t notes_extra_life[] = {239, 190, 159, 119, 159, 119, 95, 80};
static const uint16_t notes_game_over[] = {190, 213, 239, 284, 358, 426};
static const uint16_t notes_wave_start[] = {190, 159};

/* a soft pew: starts around 830 Hz and sweeps down to about 440 Hz while fading out */
static const SfxDef def_shoot = {12, 150, 12, 0, 9, 1, NULL, 0};
static const SfxDef def_explode_large = {32, 0, 0, 28, 15, 2, NULL, 0};
static const SfxDef def_explode_medium = {22, 0, 0, 20, 14, 2, NULL, 0};
static const SfxDef def_explode_small = {14, 0, 0, 12, 13, 1, NULL, 0};
static const SfxDef def_death_noise = {45, 0, 0, 31, 15, 3, NULL, 0};
static const SfxDef def_death_tone = {40, 150, 6, 0, 11, 4, NULL, 0};
static const SfxDef def_powerup = {12, 239, 0, 0, 11, 0, notes_powerup, 3};
static const SfxDef def_extra_life = {32, 239, 0, 0, 12, 0, notes_extra_life, 4};
static const SfxDef def_hyperspace = {16, 400, -20, 0, 12, 2, NULL, 0};
static const SfxDef def_enemy_shot = {14, 200, -14, 0, 10, 1, NULL, 0};
static const SfxDef def_boss_hit_noise = {10, 0, 0, 8, 12, 1, NULL, 0};
static const SfxDef def_boss_hit_tone = {8, 300, 0, 0, 8, 1, NULL, 0};
static const SfxDef def_game_over = {72, 190, 0, 0, 12, 0, notes_game_over, 12};
static const SfxDef def_wave_start = {12, 190, 0, 0, 10, 0, notes_wave_start, 6};
static const SfxDef def_menu = {4, 119, 0, 0, 10, 0, NULL, 0};

typedef struct SfxEntry {
    const SfxDef *voice_a;
    const SfxDef *voice_b;
} SfxEntry;

static const SfxEntry sfx_table[SFX_COUNT] = {
    {NULL, NULL},
    {&def_shoot, NULL},
    {NULL, &def_explode_large},
    {NULL, &def_explode_medium},
    {NULL, &def_explode_small},
    {&def_death_tone, &def_death_noise},
    {&def_powerup, NULL},
    {&def_extra_life, NULL},
    {&def_hyperspace, NULL},
    {&def_enemy_shot, NULL},
    {&def_boss_hit_tone, &def_boss_hit_noise},
    {&def_game_over, NULL},
    {&def_wave_start, NULL},
    {&def_menu, NULL}
};

#define THRUST_NOISE 26
#define THRUST_VOLUME 6
#define UFO_TONE_HIGH 180
#define UFO_TONE_LOW 220
#define UFO_VOLUME 5
#define UFO_WARBLE_FRAMES 3

typedef struct Voice {
    const SfxDef *def;
    uint8_t frame;
} Voice;

static SoundWriter writer;
static Voice voices[2];          /* A and B; C is driven by the loop flags */
static int thrust_on;
static int ufo_on;
static uint8_t ufo_frame;
static uint8_t shadow[11];       /* last value written to each register */
static uint8_t shadow_valid[11];

static void write_register(uint8_t reg, uint8_t value) {
    if (writer != NULL && (!shadow_valid[reg] || shadow[reg] != value)) {
        writer(reg, value);
        shadow[reg] = value;
        shadow_valid[reg] = 1;
    }
}

static void write_tone(int voice, uint16_t period) {
    write_register((uint8_t) REG_TONE(voice), (uint8_t) (period & 0xff));
    write_register((uint8_t) (REG_TONE(voice) + 1), (uint8_t) ((period >> 8) & 0x0f));
}

/* What voice v currently sounds like. Returns 0 when its effect has finished. */
static int voice_state(Voice *voice, uint16_t *tone, uint8_t *noise, uint8_t *volume) {
    const SfxDef *def = voice->def;
    int level;

    if (def == NULL) {
        return 0;
    }
    if (voice->frame >= def->frames) {
        voice->def = NULL;
        return 0;
    }

    if (def->notes != NULL) {
        *tone = def->notes[voice->frame / def->note_frames];
    } else {
        *tone = (uint16_t) ((int) def->tone + (int) def->step * (int) voice->frame);
    }
    if (def->tone == 0) {
        *tone = 0;
    }
    *noise = def->noise;
    level = def->volume;
    if (def->decay != 0) {
        level -= voice->frame / def->decay;
    }
    if (level < 0) {
        level = 0;
    }
    *volume = (uint8_t) level;
    return level > 0;
}

void sound_silence(void) {
    int index;

    voices[0].def = NULL;
    voices[1].def = NULL;
    thrust_on = 0;
    ufo_on = 0;
    for (index = 0; index < 11; ++index) {
        shadow_valid[index] = 0;
    }
    write_register(REG_VOLUME(0), 0);
    write_register(REG_VOLUME(1), 0);
    write_register(REG_VOLUME(2), 0);
    write_register(REG_MIXER, 0xff);
}

void sound_init(SoundWriter write) {
    writer = write;
    sound_silence();
}

void sound_play(int sfx) {
    if (sfx <= SFX_NONE || sfx >= SFX_COUNT) {
        return;
    }
    if (sfx_table[sfx].voice_a != NULL) {
        voices[VOICE_A].def = sfx_table[sfx].voice_a;
        voices[VOICE_A].frame = 0;
    }
    if (sfx_table[sfx].voice_b != NULL) {
        voices[VOICE_B].def = sfx_table[sfx].voice_b;
        voices[VOICE_B].frame = 0;
    }
}

void sound_set_thrust(int on) {
    thrust_on = on != 0;
}

void sound_set_ufo(int on) {
    ufo_on = on != 0;
}

void sound_tick(void) {
    uint8_t tone_off = 0x07;    /* bit per voice: 1 = tone switched off */
    uint8_t noise_off = 0x07;
    uint8_t noise_period = 0;
    int voice;

    for (voice = VOICE_A; voice <= VOICE_B; ++voice) {
        uint16_t tone = 0;
        uint8_t noise = 0;
        uint8_t volume = 0;

        if (voice_state(&voices[voice], &tone, &noise, &volume)) {
            if (tone != 0) {
                write_tone(voice, tone);
                tone_off = (uint8_t) (tone_off & ~(1 << voice));
            }
            if (noise != 0) {
                noise_period = noise;
                noise_off = (uint8_t) (noise_off & ~(1 << voice));
            }
            write_register((uint8_t) REG_VOLUME(voice), volume);
            ++voices[voice].frame;
        } else {
            write_register((uint8_t) REG_VOLUME(voice), 0);
        }
    }

    /* voice C: thrust rumble wins over the UFO warble */
    if (thrust_on) {
        if (noise_period == 0) {
            noise_period = THRUST_NOISE;
        }
        noise_off = (uint8_t) (noise_off & ~(1 << VOICE_C));
        write_register(REG_VOLUME(VOICE_C), THRUST_VOLUME);
    } else if (ufo_on) {
        write_tone(VOICE_C, ((ufo_frame / UFO_WARBLE_FRAMES) & 1) ? UFO_TONE_HIGH : UFO_TONE_LOW);
        tone_off = (uint8_t) (tone_off & ~(1 << VOICE_C));
        write_register(REG_VOLUME(VOICE_C), UFO_VOLUME);
        ++ufo_frame;
    } else {
        write_register(REG_VOLUME(VOICE_C), 0);
    }

    if (noise_period != 0) {
        write_register(REG_NOISE, noise_period);
    }
    write_register(REG_MIXER, (uint8_t) (MIXER_PORTS_OUT | tone_off | (noise_off << 3)));
}
