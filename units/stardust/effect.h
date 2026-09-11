#pragma once
// Stardust — sparkling pitched-up grains + reverb.
// X=SPARKLE Y=SIZE DEPTH=mix MODE=TWINKLE/COMET/NOVA/DRIFT
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SPARKLE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_TWINKLE = 0, M_COMET, M_NOVA, M_DRIFT, NUM_MODES };
  struct Params { float sparkle = 0.5f, size = 0.4f, depth = 0.f; uint32_t mode = M_TWINKLE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SPARKLE: params_.sparkle = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"TWINKLE", "COMET", "NOVA", "DRIFT"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float pitch = 2.f, density = 0.4f;
    switch (p.mode) { case M_TWINKLE: pitch = 2.f; density = 0.35f; break;
      case M_COMET: pitch = 3.f; density = 0.5f; break;
      case M_NOVA: pitch = 4.f; density = 0.9f; break;
      case M_DRIFT: pitch = 1.5f; density = 0.3f; break; }
    density = dsp::clampf(density + p.sparkle * 0.4f, 0.f, 1.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, density, p.size * 0.6f, pitch, 1.f, p.sparkle, false, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, 0.8f, 0.1f);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.5f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.5f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
