#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROGRAM=${1:-"$ROOT_DIR/build/asteroids-stfm.tos"}
TOS_IMAGE=${HATARI_TOS:-${2:-}}

if [ ! -f "$PROGRAM" ]; then
    echo "Missing Atari build: $PROGRAM" >&2
    echo "Run 'make atari-st' first." >&2
    exit 1
fi

if [ -z "$TOS_IMAGE" ]; then
    echo "Set HATARI_TOS to an EmuTOS/TOS image path or pass it as the second argument." >&2
    exit 1
fi

exec hatari \
    --machine st \
    --memsize 1 \
    --tos "$TOS_IMAGE" \
    --auto "$PROGRAM"
