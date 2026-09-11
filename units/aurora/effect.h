#pragma once
// Aurora — slowly evolving grains + modulated reverb.
// X=DRIFT Y=SIZE DEPTH=mix MODE=DAWN/NIGHT/SOLAR/POLAR
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { DRIFT = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_DAWN = 0, M_NIGHT, M_SOLAR, M_POLAR, NUM_MODES };
  struct Params { float drift = 0.3f, size = 0.6f, depth = 0.f; uint32_t mode = M_DAWN; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case DRIFT: params_.drift = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"DAWN", "NIGHT", "SOLAR", "POLAR"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_);
    params_ = Params(); glide_ = 0.5f;
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float damp = 0.4f; float pitch = 1.f;
    switch (p.mode) { case M_DAWN: pitch = 1.f; damp = 0.3f; break; case M_NIGHT: pitch = 0.5f; damp = 0.6f; break;
      case M_SOLAR: pitch = 2.f; damp = 0.15f; break; case M_POLAR: pitch = 1.5f; damp = 0.5f; break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    const float driftRate = 0.0000008f + p.drift * 0.000006f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      glide_ += driftRate; if (glide_ >= 1.f) glide_ -= 1.f;
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 0.6f, p.size, pitch, 0.7f, glide_, false, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, 0.85f, damp);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.4f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.4f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f; float glide_ = 0.5f;
};
