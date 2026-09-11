#pragma once
// Mist — light sparse grains + soft reverb haze.
// X=DENSITY Y=SIZE DEPTH=mix MODE=FOG/HAZE/DAMP/DEW
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { DENSITY = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_FOG = 0, M_HAZE, M_DAMP, M_DEW, NUM_MODES };
  struct Params { float density = 0.4f, size = 0.5f, depth = 0.f; uint32_t mode = M_FOG; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case DENSITY: params_.density = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FOG", "HAZE", "DAMP", "DEW"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float damp = 0.5f, spread = 0.7f;
    switch (p.mode) { case M_FOG: damp = 0.5f; break; case M_HAZE: damp = 0.35f; spread = 1.f; break;
      case M_DAMP: damp = 0.75f; break; case M_DEW: damp = 0.25f; spread = 0.4f; break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, p.density * 0.5f, dsp::clampf(0.5f + p.size * 0.5f, 0.f, 1.f), 1.f, spread, 0.5f, false, gl, gr);
      float rl, rr; verb_.process(gl + dry * 0.2f, gr + dry * 0.2f, rl, rr, 0.7f + p.size * 0.2f, damp);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.3f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.3f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
