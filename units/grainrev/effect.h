#pragma once
// GrainRev — reverse grain cloud.
// X=SCATTER Y=SIZE DEPTH=mix MODE=SLOW/MED/FAST/CHAOS
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SCATTER = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SLOW = 0, M_MED, M_FAST, M_CHAOS, NUM_MODES };
  struct Params { float scatter = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_MED; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SCATTER: params_.scatter = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SLOW", "MED", "FAST", "CHAOS"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float density = 0.4f, pitch = -1.f;  // negative => reverse playback
    switch (p.mode) { case M_SLOW: density = 0.3f; pitch = -0.5f; break;
      case M_MED: density = 0.5f; pitch = -1.f; break;
      case M_FAST: density = 0.9f; pitch = -1.5f; break;
      case M_CHAOS: density = 1.f; pitch = -(0.5f + p.scatter * 1.5f); break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, density, p.size, pitch, p.scatter, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; Params params_; float mDrive_ = 0.f;
};
