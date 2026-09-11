#pragma once
// Nimbus — soft grain bloom into reverb.
// X=POSITION Y=DENSITY DEPTH=mix MODE=SOFT/DENSE/FREEZE/BLOOM
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { POSITION = 0U, DENSITY, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_DENSE, M_FREEZE, M_BLOOM, NUM_MODES };
  struct Params { float position = 0.5f, density = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case POSITION: params_.position = param_10bit_to_f32(v); break;
      case DENSITY: params_.density = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "DENSE", "FREEZE", "BLOOM"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    bool frz = false; float gsize = 0.5f; float verbAmt = 0.6f;
    switch (p.mode) { case M_SOFT: gsize = 0.5f; break; case M_DENSE: gsize = 0.3f; break;
      case M_FREEZE: frz = true; gsize = 0.6f; break; case M_BLOOM: gsize = 0.8f; verbAmt = 0.9f; break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, p.density, gsize, 1.f, 0.6f, p.position, frz, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, 0.8f, 0.35f);
      out[0] = dsp::lerp(in[0], gl + rl * verbAmt, mix); out[1] = dsp::lerp(in[1], gr + rr * verbAmt, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_;
};
