#!/usr/bin/env bash
# Fan shared/dsp.h out to every unit that uses it.
# shared/dsp.h is the canonical copy; units/*/dsp.h are generated mirrors
# (the SDK build stages each unit directory on its own, so each needs a local copy).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/shared/dsp.h"
[ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
n=0
for d in "$root"/units/*/; do
  [ -f "$d/dsp.h" ] || continue   # only units that already use it (skips gritcrush)
  cp "$src" "$d/dsp.h"
  n=$((n+1))
done
echo "synced shared/dsp.h -> $n units"
