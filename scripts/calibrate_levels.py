#!/usr/bin/env python3
"""Level-match every unit's modes: full-wet output ~ dry input on pink noise.

Each unit carries one line in effect.h,
    static const float kLevel[NUM_MODES] = {...};  // @auto-level
which scales its wet signal per MODE. This script measures every mode with
tools/probe (via scripts/probe_units.sh, DRIVE 0), rescales those numbers so
each mode lands on the target, rewrites the line, and re-measures.

Modes that are meant to be quiet (e.g. a ducker that hides under the input)
opt out with a comment listing mode indices:   // @level-exempt 2

Usage: scripts/calibrate_levels.py [--target DB] [--check] [unit ...]
  --check   only report, change nothing (non-zero exit if anything is off)
Run it after changing a unit's sound; then rebuild.
"""
import argparse, math, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LINE = re.compile(r"(static const float kLevel\[\w+\] = \{)([^}]*)(\};\s*// @auto-level)")
TOL_DB = 0.6
LIMITS = (0.2, 5.0)  # a trim outside this means the unit itself needs fixing


def measure(unit):
    out = subprocess.run([str(ROOT / "scripts/probe_units.sh"), unit], capture_output=True, text=True, check=True)
    tsv = (ROOT / "tools/probe/build/results.tsv").read_text().splitlines()
    return [float(l.split("\t")[2]) for l in tsv if l.startswith(unit + "\t")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", type=float, default=0.0)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("units", nargs="*")
    a = ap.parse_args()
    units = a.units or sorted(p.name for p in (ROOT / "units").iterdir() if (p / "effect.h").exists())
    bad = 0
    for u in units:
        f = ROOT / "units" / u / "effect.h"
        src = f.read_text()
        m = LINE.search(src)
        if not m:
            continue
        exempt = {int(x) for x in re.findall(r"@level-exempt\s+([\d\s]+)", src)[0].split()} if "@level-exempt" in src else set()
        cur = [float(x.strip().rstrip("f")) for x in m.group(2).split(",")]
        for attempt in range(3):
            lv = measure(u)
            off = [i for i, d in enumerate(lv) if i not in exempt and abs(d - a.target) > TOL_DB]
            if not off or a.check:
                break
            for i in off:
                cur[i] *= 10 ** ((a.target - lv[i]) / 20)
                cur[i] = min(max(cur[i], LIMITS[0]), LIMITS[1])
            src = LINE.sub(lambda mm: mm.group(1) + ", ".join(f"{c:.3f}f" for c in cur) + mm.group(3), src, count=1)
            f.write_text(src)
        lv = measure(u) if not a.check else lv
        worst = max((abs(d - a.target) for i, d in enumerate(lv) if i not in exempt), default=0)
        pinned = [i for i, c in enumerate(cur) if c in LIMITS]
        ok = worst <= TOL_DB and not pinned
        bad += not ok
        print(f"{u:11s} {'ok ' if ok else 'OFF'} " + "  ".join(
            f"{d:+5.1f}dB{'*' if i in exempt else ''}" for i, d in enumerate(lv))
            + f"   trims {', '.join(f'{c:.2f}' for c in cur)}" + (f"  (pinned at limit: {pinned})" if pinned else ""))
    print(f"\n{len(units) - bad} ok, {bad} off (target {a.target:+.1f} dB, tolerance {TOL_DB} dB; * = exempt)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
