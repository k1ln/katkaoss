// Minimal stand-in for the SDK's unit_genericfx.h so a unit's effect.h can be
// compiled natively (on the host) by tools/probe. Only what effect.h files use.
#pragma once
#include <stdint.h>
#define param_10bit_to_f32(val) ((uint16_t)(val) * 9.77517106549365e-004f)
enum {
  k_unit_touch_phase_began = 0U,
  k_unit_touch_phase_moved,
  k_unit_touch_phase_ended,
  k_unit_touch_phase_stationary,
  k_unit_touch_phase_cancelled
};
