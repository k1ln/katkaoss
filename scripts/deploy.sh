#!/usr/bin/env bash
set -euo pipefail

# Collect every built .nts3unit into a single deploy/ folder so they're easy to
# find and select in KORG KONTROL Editor.
#
# Usage:
#   scripts/deploy.sh            Copy all existing .nts3unit files into deploy/
#   scripts/deploy.sh --build    Build every unit first, then copy

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
UNITS_DIR="$ROOT_DIR/units"
DEPLOY_DIR="$ROOT_DIR/deploy"

if [ "${1:-}" = "--build" ]; then
  for d in "$UNITS_DIR"/*/; do
    u="$(basename "$d")"
    echo ">> Building $u"
    "$SCRIPT_DIR/build_unit.sh" "$u" >/dev/null
  done
fi

rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"

count=0
for f in "$UNITS_DIR"/*/*.nts3unit; do
  [ -e "$f" ] || continue
  cp "$f" "$DEPLOY_DIR/"
  count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
  echo "No .nts3unit files found. Build first with: scripts/deploy.sh --build"
  exit 1
fi

echo ""
echo "Copied $count unit(s) into deploy/:"
ls -1 "$DEPLOY_DIR"
echo ""
echo "Point KORG KONTROL Editor at the deploy/ folder to install them."
