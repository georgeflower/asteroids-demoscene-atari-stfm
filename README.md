# asteroids-demoscene-atari-stfm

Native Atari STFM 1040 adaptation of an asteroid-action DemoScene project, targeted first at real 68000 hardware and secondarily at the Hatari emulator.

## What is in this repository

This port keeps the code small and readable by splitting the project into a portable game core plus Atari-specific low-level code:

- `src/game.c`: portable fixed-point game loop, asteroid spawning/splitting, ship movement, bullet updates, collisions, and vector-shape rendering callbacks
- `src/platform_atari_st.c`: Atari ST specific video setup, raw IKBD key-state polling, palette setup, double buffering, and resolution switching
- `src/main.c`: tiny native entry point that wires the core loop to the ST platform layer
- `src/st_video.S`: 68000 assembly routines for screen clear and low-resolution line rendering, inspired by the structure used in `quanoid-st-src.zip`
- `src/st_ikbd.S`: IKBD (keyboard) interrupt handler that keeps held/released state for every scan code; TOS `Bconin` only reports key presses, so the game installs its own handler on the ACIA interrupt while it runs

The implementation direction was adjusted using the `Atari_ST_Sources` archive as historical reference and the `quanoid-st-src.zip` source archive in this repository as inspiration for practical ST build/runtime structure, while keeping the asteroid game logic specific to this project.

## Atari ST target

- CPU: Motorola 68000
- Machine target: Atari 1040 STFM class hardware
- Default video mode: 320x200 low resolution
- Alternate video mode: 640x200 medium resolution (toggle with `M`/`Tab`/`F5`)
- Frame pacing: vertical blank (`Vsync`) driven loop
- Rendering: software vector drawing into double-buffered ST planar framebuffers, with 68000 assembly acceleration in low resolution
- Input: persistent raw IKBD scan-code state polling suitable for real hardware action controls

### Supported controls

- left arrow or `A`: rotate left
- right arrow or `D`: rotate right
- up arrow or `W`: thrust
- `Space`: fire
- `M`, `Tab`, or `F5`: switch between 320x200 and 640x200 modes
- `Esc` or `Q`: quit

If no manual control is used, the ship falls back to a small attract/demo autopilot so the program still behaves like a self-running demoscene display.

## Building

The native target expects the MiNT/Atari cross-toolchain such as `m68k-atari-mint-gcc`.

```sh
make atari-st
```

Output:

- `build/ASTROIDS.PRG`

To create a 720 KB floppy image for real hardware or emulators (`mtools` required):

```sh
make disk-image
```

Output:

- `build/asteroids-stfm.st`

### Tested local validation in this repository

This repository also includes a tiny host-side validation target for the portable game logic:

```sh
make test
```

It checks that:

- the initial wave spawns correctly
- bullet/asteroid collisions split large asteroids
- ship coordinates wrap back onto the playfield

## Running on real hardware

- Build `build/ASTROIDS.PRG`
- Build `build/asteroids-stfm.st`
- Write the `.st` image to a 720 KB disk with your preferred Atari-capable imaging workflow, or copy `ASTROIDS.PRG` onto a DOS-formatted Atari floppy
- Boot the Atari 1040 STFM and launch `ASTROIDS.PRG`

## Running in Hatari

You need Hatari plus a TOS or EmuTOS ROM image on your machine.

Quick autostart of the `.PRG`:

```sh
HATARI_TOS=/path/to/tos.img hatari/run-hatari.sh
```

Boot the floppy image instead:

```sh
hatari/run-hatari.sh build/asteroids-stfm.st /path/to/tos.img
```

The script uses either:

- `--harddrive <build-dir>` plus `--auto C:\\ASTROIDS.PRG` for fast emulator testing
- or `--disk-a build/asteroids-stfm.st` when you want to boot the same floppy image used for real hardware

## Hardware assumptions and limitations

### Assumptions

- 1 MB STFM class memory budget
- TOS-compatible startup/runtime environment
- Native ST low/medium resolution planar framebuffer layout
- Keyboard-only input through IKBD scan codes

### Limitations

- The port is intentionally minimal: no sampled audio, GEM UI, joystick layer, or original asset pipeline
- Hatari/TOS ROMs are not bundled here
- The original upstream project could not be diffed directly from this sandbox, so gameplay preservation is based on a reasonable Asteroids-style adaptation instead of verified one-to-one behavior
- The code targets 320x200 and 640x200 ST modes; STFM hardware does not provide an 8-bit chunky framebuffer, so color usage stays within native ST planar limits
- Validation in this environment covers the portable game logic plus native artifact creation; sustained play-testing still needs real STFM hardware or Hatari with a TOS ROM
