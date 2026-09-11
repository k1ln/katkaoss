#pragma once
// Stutter — granular freeze / beat-repeat glitch.
// X=POSITION Y=SIZE DEPTH=mix MODE=QTR/8TH/16TH/ROLL
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { POSITION = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_QTR = 0, M_8TH, M_16TH, M_ROLL, NUM_MODES };
  struct Params { float position = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_8TH; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case POSITION: params_.position = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"QTR", "8TH", "16TH", "ROLL"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    // smaller grains + higher density towards ROLL => faster stutter
    float gsize = p.size; float density = 1.f;
    switch (p.mode) { case M_QTR: gsize *= 0.9f; density = 0.6f; break;
      case M_8TH: gsize *= 0.6f; density = 1.f; break;
      case M_16TH: gsize *= 0.35f; density = 1.f; break;
      case M_ROLL: gsize *= 0.15f; density = 1.f; break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, density, gsize, 1.f, 0.25f, p.position, true, gl, gr);  // freeze = repeat
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; Params params_; float mDrive_ = 0.f;
};
