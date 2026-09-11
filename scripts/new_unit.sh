#!/usr/bin/env bash
set -euo pipefail

# Scaffold a new NTS-3 kaoss pad kit "genericfx" unit project from the
# logue-sdk dummy-genericfx template.
#
# Usage: scripts/new_unit.sh <unit-name> "<Display Name>" <dev_id_hex> [unit_id_hex]

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TEMPLATE_DIR="$ROOT_DIR/logue-sdk/platform/nts-3_kaoss/dummy-genericfx"
UNITS_DIR="$ROOT_DIR/units"

usage() {
  echo "Usage: $0 <unit-name> \"<Display Name>\" <dev_id_hex> [unit_id_hex]"
  echo "  unit-name:    directory/project slug, e.g. 'my-crusher' (lowercase, hyphens ok)"
  echo "  Display Name: up to 19 chars, shown on device. [A-Za-z0-9 -_]"
  echo "  dev_id_hex:   your developer ID, e.g. 0x00000001 (see logue-sdk/developer_ids.md)"
  echo "  unit_id_hex:  optional unit ID within your dev ID scope (default: 0x0)"
  exit 1
}

[ $# -ge 3 ] || usage

UNIT_NAME="$1"
DISPLAY_NAME="$2"
DEV_ID="$3"
UNIT_ID="${4:-0x0}"

DEST_DIR="$UNITS_DIR/$UNIT_NAME"

[ -d "$TEMPLATE_DIR" ] || {
  echo "Error: template not found at $TEMPLATE_DIR"
  echo "Did you run: git submodule update --init logue-sdk/platform/ext/CMSIS ?"
  exit 1
}
[ ! -e "$DEST_DIR" ] || {
  echo "Error: $DEST_DIR already exists"
  exit 1
}
[ "${#DISPLAY_NAME}" -le 19 ] || {
  echo "Error: display name must be 19 characters or fewer"
  exit 1
}

mkdir -p "$UNITS_DIR"
cp -R "$TEMPLATE_DIR" "$DEST_DIR"
rm -rf "$DEST_DIR/build" "$DEST_DIR/.dep"

PROJECT_SLUG="${UNIT_NAME//-/_}"

sed -i.bak "s/^PROJECT := .*/PROJECT := ${PROJECT_SLUG}/" "$DEST_DIR/config.mk"
sed -i.bak "s/\.name = \"dummy\"/.name = \"${DISPLAY_NAME}\"/" "$DEST_DIR/header.c"
sed -i.bak "s/\.dev_id = 0x0,/.dev_id = ${DEV_ID},/" "$DEST_DIR/header.c"
sed -i.bak "s/\.unit_id = 0x0U,/.unit_id = ${UNIT_ID}U,/" "$DEST_DIR/header.c"
find "$DEST_DIR" -name '*.bak' -delete

echo "Created new unit project at units/$UNIT_NAME"
echo ""
echo "Next steps:"
echo "  1. Edit units/$UNIT_NAME/effect.h and units/$UNIT_NAME/unit.cc to implement your effect."
echo "  2. Build it:   scripts/build_unit.sh $UNIT_NAME"
echo "  3. Upload units/$UNIT_NAME/$PROJECT_SLUG.nts3unit via KORG KONTROL Editor."
