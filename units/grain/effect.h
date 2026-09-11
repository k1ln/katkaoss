#pragma once
// Grain — granular delay / scatter engine.
// X=SCATTER  Y=SIZE  DEPTH=mix  MODE=FWD/REV/PITCH/WILD
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SCATTER = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_FWD = 0, M_REV, M_PITCH, M_WILD, NUM_MODES };

  struct Params { float scatter = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_FWD; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SCATTER: params_.scatter = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FWD", "REV", "PITCH", "WILD"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float pitch = 1.f;
    switch (p.mode) {
      case M_FWD: pitch = 1.f; break;
      case M_REV: pitch = -1.f; break;  // negative inc walks the buffer backwards
      case M_PITCH: pitch = 0.5f + p.size; break;
      case M_WILD: pitch = 0.5f + p.scatter * 1.5f; break;
    }
    const float density = 0.4f + p.scatter * 0.6f;
    const float spread = p.scatter;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr;
      cloud_.process(dry, density, p.size, pitch, spread, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  Params params_; float mDrive_ = 0.f;
};
