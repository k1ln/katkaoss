#pragma once
/*
 *  File: effect.h — Shimmer
 *
 *  Pitch-shifted (octave-up) shimmer reverb.
 *  X = SIZE      (reverb size / decay)
 *  Y = SHIMMER   (amount of pitched feedback fed back into the tail)
 *  DEPTH = dry/wet
 *  INTERVAL = OCT+ / 5TH / OCT- / DUAL
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }

  enum { SIZE = 0U, SHIMMER, DEPTH, INTERVAL, NUM_PARAMS };
  enum { I_OCTUP = 0, I_FIFTH, I_OCTDOWN, I_DUAL, NUM_INTERVALS };

  struct Params {
    float size = 0.6f;
    float shimmer = 0.5f;
    float depth = 0.f;
    uint32_t interval = I_OCTUP;
  };

  inline void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case SIZE: params_.size = param_10bit_to_f32(value); break;
      case SHIMMER: params_.shimmer = param_10bit_to_f32(value); break;
      case DEPTH: params_.depth = value / 1000.f; break;
      case INTERVAL: params_.interval = value; break;
      default: break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *iv[NUM_INTERVALS] = {"OCT+", "5TH", "OCT-", "DUAL"};
    if (index == INTERVAL && value >= 0 && value < NUM_INTERVALS) return iv[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final {
    alloc_.init(allocated_buffer, getBufferSize());
    verb_.init(alloc_);
    shifter_.init(alloc_.alloc(24000), 24000);
    params_ = Params();
    fbL_ = fbR_ = 0.f;
  }

  void teardown() override final {}
  void reset() override final { fbL_ = fbR_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float ratio = 2.f;
    switch (p.interval) {
      case I_OCTUP: ratio = 2.f; break;
      case I_FIFTH: ratio = 1.5f; break;
      case I_OCTDOWN: ratio = 0.5f; break;
      case I_DUAL: ratio = 2.f; break;
      default: break;
    }
    const float mix = (p.depth + 1.f) * 0.5f;
    const float shim = p.shimmer;

    for (const float *end = out + frames * 2; out != end; in += 2, out += 2) {
      const float dryMono = (in[0] + in[1]) * 0.5f;
      // pitch-shift the current reverb tail and feed it back in
      float pitched = shifter_.process((fbL_ + fbR_) * 0.5f, ratio);
      float feed = dryMono + pitched * shim * 0.7f;

      float rl, rr;
      verb_.process(feed, feed, rl, rr, 0.6f + p.size * 0.38f, 0.35f);
      fbL_ = rl;
      fbR_ = rr;

      out[0] = dsp::lerp(in[0], rl, mix);
      out[1] = dsp::lerp(in[1], rr, mix);
    }
  }

  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::PitchShifter shifter_;
  Params params_;
  float fbL_ = 0.f, fbR_ = 0.f;
};
