#pragma once
/*
 *  File: effect.h — Space
 *
 *  Classic Freeverb-style room / hall / plate reverb.
 *  X = SIZE   (decay length)
 *  Y = TONE   (high-frequency damping)
 *  DEPTH = dry/wet
 *  TYPE = ROOM / HALL / PLATE / VAST
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }

  enum { SIZE = 0U, TONE, DEPTH, TYPE, NUM_PARAMS };
  enum { T_ROOM = 0, T_HALL, T_PLATE, T_VAST, NUM_TYPES };

  struct Params {
    float size = 0.5f;
    float tone = 0.5f;
    float depth = 0.f;
    uint32_t type = T_HALL;
  };

  inline void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case SIZE: params_.size = param_10bit_to_f32(value); break;
      case TONE: params_.tone = param_10bit_to_f32(value); break;
      case DEPTH: params_.depth = value / 1000.f; break;
      case TYPE: params_.type = value; break;
      default: break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *ty[NUM_TYPES] = {"ROOM", "HALL", "PLATE", "VAST"};
    if (index == TYPE && value >= 0 && value < NUM_TYPES) return ty[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final {
    alloc_.init(allocated_buffer, getBufferSize());
    verb_.init(alloc_);
    preL_.reset();
    preR_.reset();
    params_ = Params();
  }

  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float baseSize = 0.5f, extra = 0.f;
    switch (p.type) {
      case T_ROOM: baseSize = 0.45f; break;
      case T_HALL: baseSize = 0.7f; break;
      case T_PLATE: baseSize = 0.6f; break;
      case T_VAST: baseSize = 0.85f; extra = 0.05f; break;
      default: break;
    }
    const float room = dsp::clampf(baseSize + p.size * 0.35f, 0.f, 1.f);
    const float damp = 1.f - p.tone;  // TONE up = brighter
    const float mix = (p.depth + 1.f) * 0.5f;

    for (const float *end = out + frames * 2; out != end; in += 2, out += 2) {
      float rl, rr;
      verb_.process(in[0], in[1], rl, rr, room, damp, extra);
      out[0] = dsp::lerp(in[0], rl, mix);
      out[1] = dsp::lerp(in[1], rr, mix);
    }
  }

  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::OnePole preL_, preR_;
  Params params_;
};
