#pragma once
// SpringVerb — dispersive spring tank.
// X=TENSION Y=TONE DEPTH=mix MODE=1SPR/2SPR/3SPR/DRIP
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TENSION = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_1 = 0, M_2, M_3, M_DRIP, NUM_MODES };
  struct Params { float tension = 0.5f, tone = 0.5f, depth = 0.f; uint32_t mode = M_2; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case TENSION: params_.tension = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"1SPR", "2SPR", "3SPR", "DRIP"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    static const uint32_t len[kAP] = {149, 211, 263, 337, 401};
    for (int i = 0; i < kAP; ++i) ap_[i].init(alloc_.alloc(len[i]), len[i]);
    verb_.init(alloc_); params_ = Params(); fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    int stages = 3; float g = 0.6f + p.tension * 0.35f;
    switch (p.mode) { case M_1: stages = 2; break; case M_2: stages = 3; break;
      case M_3: stages = 5; break; case M_DRIP: stages = 5; g = 0.85f; break; }
    const float damp = 1.f - p.tone;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float x = dry + fb_ * 0.4f;
      for (int i = 0; i < stages; ++i) x = ap_[i].process(x, g);  // dispersion "boing"
      float rl, rr; verb_.process(x, x, rl, rr, 0.55f + p.tension * 0.3f, damp);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  static constexpr int kAP = 5;
  dsp::BufferAllocator alloc_; dsp::Allpass ap_[kAP]; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f; float fb_ = 0.f;
};
