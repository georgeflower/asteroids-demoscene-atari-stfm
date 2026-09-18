# asteroids-demoscene-atari-stfm

Native Atari STFM 1040 adaptation of an Asteroids-style DemoScene project, targeted at 68000 hardware and the Hatari emulator.

## What is in this repository

This port keeps the code small and readable by splitting the project into two clear paths:

- `src/game.c`: portable fixed-point game loop, asteroid spawning/splitting, ship movement, bullet updates, collisions, and vector-shape rendering callbacks
- `src/platform_atari_st.c`: Atari ST specific video, keyboard polling, palette setup, frame pacing, and resolution switching
- `src/main.c`: tiny native entry point that wires the core loop to the ST platform layer

Because the original source repository was not accessible from this sandbox, this repository implements a practical ST-native adaptation layer rather than a byte-for-byte source port. The gameplay target is still the same: a wireframe ship, drifting/splitting asteroids, bullets, screen wrap, and attract-mode style autonomous play with optional keyboard control.

## Atari ST target

- CPU: Motorola 68000
- Machine target: Atari STFM 1040 class hardware
- Default video mode: 320x200 low resolution
- Alternate video mode: 640x200 medium resolution (toggle with `M`/`Tab`/`F5`)
- Frame pacing: vertical blank (`Vsync`) driven loop
- Rendering: software Bresenham vector lines drawn directly into the ST planar framebuffer
- Input: raw keyboard polling through TOS console services

### Supported controls

- `A` / left arrow: rotate left
- `D` / right arrow: rotate right
- `W` / up arrow: thrust
- `Space`: fire
- `M`, `Tab`, or `F5`: switch between 320x200 and 640x200 modes
- `Esc` or `Q`: quit

If no manual control is used, the ship falls back to a small attract/demo autopilot so the program still behaves like a self-running demoscene display.

## Building

The native target expects a MiNT/Atari cross-toolchain such as `m68k-atari-mint-gcc`.

```sh
make atari-st
```

Output:

- `build/asteroids-stfm.tos`

### Tested local validation in this repository

This repository also includes a tiny host-side validation target for the portable game logic:

```sh
make test
```

It checks that:

- the initial wave spawns correctly
- bullet/asteroid collisions split large asteroids
- ship coordinates wrap back onto the playfield

## Running in Hatari

You need Hatari plus a TOS or EmuTOS ROM image on your machine.

```sh
HATARI_TOS=/path/to/emutos.img hatari/run-hatari.sh
```

Or explicitly:

```sh
hatari/run-hatari.sh build/asteroids-stfm.tos /path/to/emutos.img
```

The script launches Hatari with:

- `--machine st`
- `--memsize 1`
- `--auto build/asteroids-stfm.tos`

## Hardware assumptions and limitations

### Assumptions

- 1 MB STFM class memory budget
- TOS-compatible startup/runtime environment
- Native ST low/medium resolution planar framebuffer layout
- Keyboard-only input

### Limitations

- The port is intentionally minimal: no sampled audio, GEM UI, joystick layer, or original asset pipeline
- Hatari/TOS ROMs are not bundled here
- The original upstream project could not be diffed directly from this sandbox, so gameplay preservation is based on a reasonable Asteroids-style adaptation instead of verified one-to-one behavior
- The code targets 320x200 and 640x200 ST modes; STFM hardware does not provide an 8-bit chunky framebuffer, so color usage stays within native ST planar limits
- Validation in this environment covers the portable game logic only; native `.tos` execution requires an external Atari cross-toolchain and Hatari installation
