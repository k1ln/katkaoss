#!/usr/bin/env bash
set -euo pipefail

# Build a KatKaoss NTS-3 kaoss pad kit unit using the logue-sdk Docker toolchain.
#
# Projects live in units/<name>/ so they stay independent of the logue-sdk
# submodule. This script stages a copy inside the submodule's platform tree
# (required by the SDK's docker build scripts), builds it, then copies the
# resulting .nts3unit file back into units/<name>/.
#
# Usage:
#   scripts/build_unit.sh <unit-name>          Build the unit
#   scripts/build_unit.sh <unit-name> --clean  Remove build artifacts

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SDK_DIR="$ROOT_DIR/logue-sdk"
UNITS_DIR="$ROOT_DIR/units"

usage() {
  echo "Usage: $0 <unit-name> [--clean]"
  exit 1
}

[ $# -ge 1 ] || usage

UNIT_NAME="$1"
MODE="${2:-build}"

SRC_DIR="$UNITS_DIR/$UNIT_NAME"
[ -d "$SRC_DIR" ] || {
  echo "Error: unit '$UNIT_NAME' not found in $UNITS_DIR"
  exit 1
}

command -v docker >/dev/null || {
  echo "Error: docker is required. Install Docker and try again."
  exit 1
}

STAGE_NAME="katkaoss_${UNIT_NAME//-/_}"
STAGE_DIR="$SDK_DIR/platform/nts-3_kaoss/$STAGE_NAME"

cleanup() { rm -rf "$STAGE_DIR"; }
trap cleanup EXIT

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
rsync -a --exclude 'build' --exclude '.dep' --exclude '*.nts3unit' "$SRC_DIR/" "$STAGE_DIR/"

pushd "$SDK_DIR/docker" >/dev/null
if [ "$MODE" = "--clean" ]; then
  ./run_cmd.sh build --clean "nts-3_kaoss/$STAGE_NAME"
else
  ./run_cmd.sh build "nts-3_kaoss/$STAGE_NAME"
fi
popd >/dev/null

if [ "$MODE" != "--clean" ]; then
  PRODUCT="$(find "$STAGE_DIR" -maxdepth 1 -name '*.nts3unit' | head -n1)"
  [ -n "$PRODUCT" ] || {
    echo "Error: build did not produce a .nts3unit file"
    exit 1
  }
  cp "$PRODUCT" "$SRC_DIR/$(basename "$PRODUCT")"
  echo ""
  echo "Built: units/$UNIT_NAME/$(basename "$PRODUCT")"
  echo "Upload it to your NTS-3 kaoss pad kit using KORG KONTROL Editor."
fi
