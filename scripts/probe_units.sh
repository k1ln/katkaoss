#!/usr/bin/env bash
# Measure units natively (no Docker, no device): level, tail, brightness,
# stereo width, CPU, and silent/NaN modes — see tools/probe/probe.cc.
#
#   scripts/probe_units.sh                 all units
#   scripts/probe_units.sh clouds freeze   just these
#   scripts/probe_units.sh --similar       all units + the most alike unit/mode pairs
#   PROBE_XY="512 800" scripts/probe_units.sh   set the X/Y pad (0..1023), default "256 512"
#
# Needs a host C++ compiler (clang++/g++). Results also land in tools/probe/build/results.tsv.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/tools/probe/build"
mkdir -p "$out"
similar=0; units=()
for a in "$@"; do
  if [ "$a" = "--similar" ]; then similar=1; else units+=("$a"); fi
done
if [ ${#units[@]} -eq 0 ]; then
  for d in "$root"/units/*/; do units+=("$(basename "$d")"); done
fi
CXX="${CXX:-clang++}"
read -r px py <<<"${PROBE_XY:-256 512}"

: >"$out/results.tsv"
printf '%-11s %-8s %8s %7s %9s %6s %7s  %s\n' unit mode "level" "tail" "bright" "L/R" "ns/smp" flags
for u in "${units[@]}"; do
  d="$root/units/$u"
  [ -f "$d/effect.h" ] || { echo "no such unit: $u" >&2; continue; }
  if ! "$CXX" -O2 -std=c++11 -w -I "$root/tools/probe/stub" \
        -I "$root/logue-sdk/platform/nts-3_kaoss/common" -I "$d" \
        -o "$out/$u" "$root/tools/probe/probe.cc" 2>"$out/$u.err"; then
    echo "$u: native compile failed (see $out/$u.err)"; continue
  fi
  "$out/$u" "$u" "$px" "$py" | tee -a "$out/results.tsv" |
    awk -F'\t' '{printf "%-11s %-8s %+6.1fdB %6.2fs %7.0fHz %6.2f %7.1f  %s\n",$1,$2,$3,$4,$5,$6,$7,$8}'
done
echo "(level = full-wet vs dry on pink noise, DRIVE 0; tail = to -60 dB, capped 20 s)"

if [ $similar -eq 1 ]; then
  python3 - "$out/results.tsv" <<'PY'
import sys, math
rows = []
for line in open(sys.argv[1]):
    head, fp = line.rstrip("\n").split("|")
    f = head.split("\t")
    v = [float(x) for x in fp.split()]
    bands, (tail, flux, corr) = v[:12], v[12:15]
    feat = [math.sqrt(b) for b in bands] + [tail * 1.0, flux * 0.5, corr * 0.5]
    rows.append((f[0], f[1], feat))
def dist(a, b): return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
pairs = []
for i in range(len(rows)):
    for j in range(i + 1, len(rows)):
        if rows[i][0] != rows[j][0]:
            pairs.append((dist(rows[i][2], rows[j][2]), rows[i], rows[j]))
pairs.sort(key=lambda p: p[0])
print("\nMost alike modes across different units (distance; lower = more alike):")
for d, a, b in pairs[:15]:
    print(f"  {d:.3f}  {a[0]:>10} {a[1]:<8} ~ {b[0]:>10} {b[1]:<8}")
nn = []
for i, a in enumerate(rows):
    best = min(dist(a[2], b[2]) for j, b in enumerate(rows) if b[0] != a[0])
    nn.append(best)
nn.sort()
med = nn[len(nn) // 2]
print(f"\nNearest-other-unit distance per mode: min {nn[0]:.3f}  median {med:.3f}  max {nn[-1]:.3f}")
within = []
units = sorted(set(r[0] for r in rows))
for u in units:
    m = [r for r in rows if r[0] == u]
    ds = [dist(m[i][2], m[j][2]) for i in range(len(m)) for j in range(i + 1, len(m))]
    within.append((sum(ds) / len(ds) if ds else 0, u))
within.sort()
print("Units whose 4 modes are least distinct from each other (mean in-unit distance):")
for d, u in within[:8]:
    print(f"  {d:.3f}  {u}")
PY
fi
