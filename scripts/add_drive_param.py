#!/usr/bin/env python3
"""One-shot patcher: add a DRIVE (grit) parameter to every dsp.h-based unit.

- header.c: bump num_params 4->5, turn the first empty param slot into DRIVE,
  and give it a default mapping (unassigned, init 250 ~ 0.24 grit).
- effect.h: declare mDrive_, handle param index 4 in setParameter, and route
  the wet output through dsp::driveMix (grit + dry/wet).
"""
import os, sys

UNITS_DIR = os.path.join(os.path.dirname(__file__), "..", "units")
SKIP = {"gritcrush"}

# --- header.c edits -------------------------------------------------------
H_NUMPARAMS_OLD = ".num_params = 4,"
H_NUMPARAMS_NEW = ".num_params = 5,"
H_EMPTY_PARAM = '{0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},'
H_DRIVE_PARAM = '{0, 1023, 0, 250, k_unit_param_type_none, 0, 0, 0, {"DRIVE"}},'
H_EMPTY_MAP = "{k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},"
H_DRIVE_MAP = "{k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 250},"

# --- effect.h edits -------------------------------------------------------
E_MEMBER_OLD = "Params params_;"
E_MEMBER_NEW = "Params params_; float mDrive_ = 0.f;"
E_DEPTH_V = "case DEPTH: params_.depth = v / 1000.f; break;"
E_DEPTH_V_NEW = "case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;"
E_DEPTH_VALUE = "case DEPTH: params_.depth = value / 1000.f; break;"
E_DEPTH_VALUE_NEW = "case DEPTH: params_.depth = value / 1000.f; break;\n      case 4: mDrive_ = param_10bit_to_f32(value); break;"


def patch_header(path):
    s = open(path).read()
    if "DRIVE" in s:  # already patched
        return False
    if H_NUMPARAMS_OLD not in s:
        raise RuntimeError(f"{path}: num_params anchor missing")
    s = s.replace(H_NUMPARAMS_OLD, H_NUMPARAMS_NEW, 1)
    s = s.replace(H_EMPTY_PARAM, H_DRIVE_PARAM, 1)
    s = s.replace(H_EMPTY_MAP, H_DRIVE_MAP, 1)
    open(path, "w").write(s)
    return True


def patch_effect(path):
    s = open(path).read()
    if "mDrive_" in s:  # already patched
        return False
    s = s.replace(E_MEMBER_OLD, E_MEMBER_NEW, 1)
    if E_DEPTH_V in s:
        s = s.replace(E_DEPTH_V, E_DEPTH_V_NEW, 1)
    elif E_DEPTH_VALUE in s:
        s = s.replace(E_DEPTH_VALUE, E_DEPTH_VALUE_NEW, 1)
    else:
        raise RuntimeError(f"{path}: DEPTH case anchor missing")
    s = s.replace("dsp::lerp(in[0], ", "dsp::driveMix(in[0], mDrive_, ")
    s = s.replace("dsp::lerp(in[1], ", "dsp::driveMix(in[1], mDrive_, ")
    open(path, "w").write(s)
    return True


def main():
    units = sorted(
        d for d in os.listdir(UNITS_DIR)
        if os.path.isdir(os.path.join(UNITS_DIR, d)) and d not in SKIP
    )
    for u in units:
        ud = os.path.join(UNITS_DIR, u)
        eff = os.path.join(ud, "effect.h")
        hdr = os.path.join(ud, "header.c")
        if not os.path.exists(os.path.join(ud, "dsp.h")):
            print(f"skip {u} (no dsp.h)")
            continue
        ph = patch_header(hdr)
        pe = patch_effect(eff)
        print(f"{u}: header={'+' if ph else '.'} effect={'+' if pe else '.'}")


if __name__ == "__main__":
    main()
