#pragma once
/*
 *  File: effect.h — Freeze
 *
 *  Infinite grain-freeze: capture the incoming texture and sustain it forever.
 *  X = POSITION (playback point in the captured buffer)
 *  Y = SIZE     (grain size / smoothness)
 *  DEPTH = dry/wet
 *  CATCH = LIVE / FREEZE / SMEAR / GLIDE
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }

  enum { POSITION = 0U, SIZE, DEPTH, CATCH, NUM_PARAMS };
  enum { C_LIVE = 0, C_FREEZE, C_SMEAR, C_GLIDE, NUM_CATCH };

  struct Params {
    float position = 0.5f;
    float size = 0.5f;
    float depth = 0.f;
    uint32_t catchMode = C_FREEZE;
  };

  inline void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case POSITION: params_.position = param_10bit_to_f32(value); break;
      case SIZE: params_.size = param_10bit_to_f32(value); break;
      case DEPTH: params_.depth = value / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(value); break;
      case CATCH: params_.catchMode = value; break;
      default: break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *c[NUM_CATCH] = {"LIVE", "FREEZE", "SMEAR", "GLIDE"};
    if (index == CATCH && value >= 0 && value < NUM_CATCH) return c[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final {
    alloc_.init(allocated_buffer, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_);
    params_ = Params();
    glide_ = 0.5f;
  }

  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    bool freeze = (p.catchMode != C_LIVE);
    float spread = (p.catchMode == C_SMEAR) ? 0.8f : 0.35f;
    float density = 1.f;

    // GLIDE slowly drifts the playback position for evolving textures
    float pos = p.position;
    if (p.catchMode == C_GLIDE) {
      glide_ += 0.0000015f;
      if (glide_ >= 1.f) glide_ -= 1.f;
      pos = glide_;
    }
    const float mix = (p.depth + 1.f) * 0.5f;

    for (const float *end = out + frames * 2; out != end; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr;
      cloud_.process(dry, density, p.size, 1.f, spread, pos, freeze, gl, gr);
      float rl, rr;
      verb_.process(gl, gr, rl, rr, 0.75f, 0.4f);
      out[0] = dsp::driveMix(in[0], mDrive_, gl + rl * 0.5f, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, gr + rr * 0.5f, mix);
    }
  }

  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f;
  float glide_ = 0.5f;
};
