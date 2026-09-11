#pragma once
// RingVerb — ring modulator -> reverb.
// X=FREQ Y=SIZE DEPTH=mix MODE=BELL/METAL/ALIEN/SUB
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { FREQ = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_BELL = 0, M_METAL, M_ALIEN, M_SUB, NUM_MODES };
  struct Params { float freq = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_BELL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case FREQ: params_.freq = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"BELL", "METAL", "ALIEN", "SUB"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); verb_.init(alloc_); osc_.reset(); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { osc_.reset(); }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float lo = 100.f, hi = 1500.f;
    switch (p.mode) { case M_BELL: lo = 200.f; hi = 1200.f; break; case M_METAL: lo = 400.f; hi = 3000.f; break;
      case M_ALIEN: lo = 50.f; hi = 800.f; break; case M_SUB: lo = 20.f; hi = 200.f; break; }
    const float hz = lo + p.freq * (hi - lo);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float ring = dry * osc_.sine(hz);
      float rl, rr; verb_.process(ring, ring, rl, rr, 0.6f + p.size * 0.35f, 0.3f);
      out[0] = dsp::driveMix(in[0], mDrive_, ring + rl * 0.7f, mix); out[1] = dsp::driveMix(in[1], mDrive_, ring + rr * 0.7f, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; dsp::Osc osc_; Params params_; float mDrive_ = 0.f;
};
