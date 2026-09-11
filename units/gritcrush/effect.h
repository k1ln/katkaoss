#pragma once
/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

/*
 *  File: effect.h
 *
 *  GritCrush: bitcrusher + sample-rate reducer with dry/wet mix.
 *
 */
#include "processor.h"
#include "unit_genericfx.h"
#include <cmath>

class Effect : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0x40000U; } // 1 MB

  // audio parameters
  enum
  {
    PARAM1 = 0U, // crush: bit-depth reduction amount
    PARAM2,      // rate: sample-rate reduction amount
    DEPTH,       // dry/wet balance
    PARAM4,      // drive: pre-gain stage before crushing
    NUM_PARAMS
  };

  // Note: Make sure that default param values correspond to declarations in header.c
  struct Params
  {
    float crush;
    float rate;
    float depth;
    uint32_t drive;

    void reset()
    {
      crush = 0.f;
      rate = 0.f;
      depth = 0.f;
      drive = 1;
    }

    Params() { reset(); }
  };

  enum
  {
    DRIVE_VALUE0 = 0,
    DRIVE_VALUE1,
    DRIVE_VALUE2,
    DRIVE_VALUE3,
    NUM_DRIVE_VALUES,
  };

  inline void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PARAM1:
      // 10bit 0-1023 parameter
      params_.crush = param_10bit_to_f32(value); // 0 .. 1023 -> 0.0 .. 1.0
      break;

    case PARAM2:
      // 10bit 0-1023 parameter
      params_.rate = param_10bit_to_f32(value); // 0 .. 1023 -> 0.0 .. 1.0
      break;

    case DEPTH:
      // Single digit base-10 fractional value, bipolar dry/wet
      params_.depth = value / 1000.f; // -1000 .. 1000 -> -1.0 .. 1.0
      break;

    case PARAM4:
      // strings type parameter, receiving index value
      params_.drive = value;
      break;

    default:
      break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    // Note: String memory must be accessible even after function returned.
    //       It can be assumed that caller will have copied or used the string
    //       before the next call to getParameterStrValue

    static const char *drive_strings[NUM_DRIVE_VALUES] = {
        "CLEAN",
        "GRIT",
        "CRUSH",
        "NUKE",
    };

    switch (index)
    {
    case PARAM4:
      if (value >= DRIVE_VALUE0 && value < NUM_DRIVE_VALUES)
        return drive_strings[value];
      break;
    default:
      break;
    }

    return nullptr;
  }

  // life-cycle methods
  void init(float *allocated_buffer) override final
  {
    buffer_ = allocated_buffer;
    params_.reset();
    reset();
  }

  void teardown() override final { buffer_ = nullptr; }

  void reset() override final
  {
    hold_l_ = 0.f;
    hold_r_ = 0.f;
    hold_counter_ = 0;
  }

  // audio processing callbacks
  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    // Caching current parameter values. Consider smoothing sensitive parameters in audio loop
    const Params p = params_;

    static constexpr float kDriveGains[NUM_DRIVE_VALUES] = {1.f, 2.f, 4.f, 8.f};
    const float drive_gain = kDriveGains[p.drive < NUM_DRIVE_VALUES ? p.drive : 0];

    // bit depth: 16 (clean) down to 2 (heavily crushed)
    const float levels = std::exp2(15.f - p.crush * 14.f);

    // sample-and-hold length: 1 (no reduction) up to 32 samples
    const uint32_t hold_len = 1U + static_cast<uint32_t>(p.rate * 31.f + 0.5f);

    // -1..1 -> 0..1 dry/wet balance ('BALN' center = equal mix)
    const float mix = (p.depth + 1.f) * 0.5f;

    for (const float *out_end = out + frames * 2; out != out_end; in += 2, out += 2)
    {
      const float dry_l = in[0];
      const float dry_r = in[1];

      if (hold_counter_ == 0)
      {
        hold_l_ = dry_l;
        hold_r_ = dry_r;
      }
      hold_counter_ = (hold_counter_ + 1) % hold_len;

      const float wet_l = crushSample(hold_l_, drive_gain, levels);
      const float wet_r = crushSample(hold_r_, drive_gain, levels);

      out[0] = dry_l + (wet_l - dry_l) * mix;
      out[1] = dry_r + (wet_r - dry_r) * mix;
    }
  }

  inline void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    // Note: Touch x/y events are already mapped to specific parameters so there is usually there no need to set parameters from here.
    //       Audio source type effects, for instance, may require these events to trigger enveloppes and such.

    (void)id;
    (void)phase;
    (void)x;
    (void)y;

    // switch (phase) {
    // case k_unit_touch_phase_began:
    //   break;
    // case k_unit_touch_phase_moved:
    //   break;
    // case k_unit_touch_phase_ended:
    //   break;
    // case k_unit_touch_phase_stationary:
    //   break;
    // case k_unit_touch_phase_cancelled:
    //   break;
    // default:
    //   break;
    // }
  }

private:
  static inline float clampf(float v, float lo, float hi)
  {
    return v < lo ? lo : (v > hi ? hi : v);
  }

  static inline float crushSample(float s, float drive_gain, float levels)
  {
    s = std::tanh(s * drive_gain);       // soft clip drive stage
    s = std::round(s * levels) / levels; // bit-depth quantization
    return clampf(s, -1.f, 1.f);
  }

  float *buffer_; // valid range:  [buffer_, buffer_ + getBufferSize())
  Params params_;

  float hold_l_ = 0.f;
  float hold_r_ = 0.f;
  uint32_t hold_counter_ = 0;
};
