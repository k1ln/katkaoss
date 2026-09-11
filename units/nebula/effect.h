#pragma once
// Nebula — ambient delay + reverb wash.
// X=TIME  Y=FEEDBACK  DEPTH=mix  MODE=SOFT/GLASS/DARK/INF
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_GLASS, M_DARK, M_INF, NUM_MODES };

  struct Params { float time = 0.5f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "GLASS", "DARK", "INF"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dlL_.init(alloc_.alloc(72000), 72000);
    dlR_.init(alloc_.alloc(72000), 72000);
    verb_.init(alloc_);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float damp = 0.25f;
    float fbCap = 0.85f;
    switch (p.mode) {
      case M_SOFT: damp = 0.4f; break;
      case M_GLASS: damp = 0.1f; break;
      case M_DARK: damp = 0.7f; break;
      case M_INF: damp = 0.3f; fbCap = 0.99f; break;
    }
    const float tL = 4800.f + p.time * 43200.f;
    const float tR = tL * 0.75f;
    const float fb = p.feedback * fbCap;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dl = dlL_.process(in[0], tL, fb, damp);
      float dr = dlR_.process(in[1], tR, fb, damp);
      float rl, rr;
      verb_.process(dl, dr, rl, rr, 0.8f, damp);
      float wl = dl + rl * 0.6f;
      float wr = dr + rr * 0.6f;
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, wr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::FBDelay dlL_, dlR_;
  dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f;
};
