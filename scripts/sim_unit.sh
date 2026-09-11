#!/usr/bin/env bash
set -euo pipefail

# Compile a KatKaoss unit to WebAssembly and run it in the SDK's device
# simulator (websim/xypad.html) so you can test it in a browser before flashing.
#
# Uses the official emscripten/emsdk Docker image, so no local emscripten
# install is needed. Output goes to units/<name>/sim/.
#
# Usage:
#   scripts/sim_unit.sh <unit-name>          Build + serve (opens a local server)
#   SIM_NO_SERVE=1 scripts/sim_unit.sh <name>   Build only
#   SIM_PORT=8080 scripts/sim_unit.sh <name>    Serve on a custom port

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:latest}"
PORT="${SIM_PORT:-8000}"

[ $# -ge 1 ] || { echo "Usage: $0 <unit-name>"; exit 1; }
UNIT="$1"
SRC="$ROOT_DIR/units/$UNIT"
[ -d "$SRC" ] || { echo "Error: unit '$UNIT' not found in units/"; exit 1; }
command -v docker >/dev/null || { echo "Error: docker is required."; exit 1; }

COMMON="/work/logue-sdk/platform/nts-3_kaoss/common"
CMSIS="/work/logue-sdk/platform/ext/CMSIS/CMSIS/Include"
WEBSIM="/work/logue-sdk/websim"

echo ">> Compiling $UNIT to WebAssembly (via $EMSDK_IMAGE)"
docker run --rm -v "$ROOT_DIR":/work -w "/work/units/$UNIT" "$EMSDK_IMAGE" bash -c "
  set -e
  rm -rf sim && mkdir -p sim
  em++ -Wno-unknown-attributes -Wno-limited-postlink-optimizations \
    -I. -I$COMMON -I$CMSIS \
    -s AUDIO_WORKLET=1 -s WASM_WORKERS=1 -lembind -O2 \
    --shell-file $WEBSIM/xypad.html \
    wasm.cc header.c unit.cc \$(ls $WEBSIM/dsp/*.c $WEBSIM/dsp/*.cpp 2>/dev/null) \
    -o sim/$UNIT.html
"

# xypad.html fetches these relative to the page
cp -r "$ROOT_DIR/logue-sdk/websim/samples" "$SRC/sim/"
cp -r "$ROOT_DIR/logue-sdk/websim/scripts" "$SRC/sim/"
cp -r "$ROOT_DIR/logue-sdk/websim/images" "$SRC/sim/"

echo ">> Built units/$UNIT/sim/$UNIT.html"

if [ "${SIM_NO_SERVE:-0}" = "1" ]; then
  echo "Skipping server (SIM_NO_SERVE=1). To run it later:"
  echo "  scripts/serve_sim.py units/$UNIT/sim $PORT"
  exit 0
fi

echo ">> Open http://localhost:$PORT/$UNIT.html in Chrome"
python3 "$SCRIPT_DIR/serve_sim.py" "$SRC/sim" "$PORT"
