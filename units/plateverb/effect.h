#pragma once
// PlateVerb — bright studio plate.
// X=SIZE Y=TONE DEPTH=mix MODE=STD/BRITE/DARK/WIDE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_STD = 0, M_BRITE, M_DARK, M_WIDE, NUM_MODES };
  struct Params { float size = 0.55f, tone = 0.6f, depth = 0.f; uint32_t mode = M_STD; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SIZE: params_.size = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"STD", "BRITE", "DARK", "WIDE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); verb_.init(alloc_); hpL_.reset(); hpR_.reset(); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float damp = 1.f - p.tone; float wide = 0.f;
    switch (p.mode) { case M_STD: break; case M_BRITE: damp *= 0.4f; break;
      case M_DARK: damp = dsp::clampf(damp + 0.4f, 0.f, 1.f); break; case M_WIDE: wide = 1.f; break; }
    const float room = dsp::clampf(0.55f + p.size * 0.35f, 0.f, 1.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr, room, damp);
      // gentle high-pass keeps the plate clear
      rl = hpL_.hp(rl, 0.02f); rr = hpR_.hp(rr, 0.02f);
      if (wide > 0.f) { float m = (rl + rr) * 0.5f, s = (rl - rr); rl = m + s; rr = m - s; }
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; dsp::OnePole hpL_, hpR_; Params params_; float mDrive_ = 0.f;
};
