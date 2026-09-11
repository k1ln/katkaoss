#pragma once
// Frostbite — icy reverse grains + bright reverb.
// X=FREEZE Y=SIZE DEPTH=mix MODE=FROST/CRACK/BLIZZARD/THAW
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { FREEZE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_FROST = 0, M_CRACK, M_BLIZZARD, M_THAW, NUM_MODES };
  struct Params { float freeze = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_FROST; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case FREEZE: params_.freeze = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FROST", "CRACK", "BLIZZARD", "THAW"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float pitch = 2.f, density = 0.6f; bool frz = (p.freeze > 0.5f);
    switch (p.mode) { case M_FROST: pitch = 2.f; density = 0.5f; break;
      case M_CRACK: pitch = -2.f; density = 0.4f; break;
      case M_BLIZZARD: pitch = 2.f; density = 1.f; break;
      case M_THAW: pitch = 1.f; density = 0.3f; frz = false; break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, density, p.size, pitch, 0.8f, p.freeze, frz, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, 0.75f + p.size * 0.2f, 0.1f);  // bright tail
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.4f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.4f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
