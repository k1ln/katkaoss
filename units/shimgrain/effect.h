#pragma once
// ShimGrain — pitched grain cloud + shimmer reverb.
// X=GRAIN Y=SHIMMER DEPTH=mix MODE=OCT/5TH/2OCT/DUST
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { GRAIN = 0U, SHIMMER, DEPTH, MODE, NUM_PARAMS };
  enum { M_OCT = 0, M_5TH, M_2OCT, M_DUST, NUM_MODES };
  struct Params { float grain = 0.5f, shimmer = 0.5f, depth = 0.f; uint32_t mode = M_OCT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case SHIMMER: params_.shimmer = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"OCT", "5TH", "2OCT", "DUST"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(72000), 72000);
    ps_.init(alloc_.alloc(24000), 24000); verb_.init(alloc_); params_ = Params(); fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float ratio = 2.f;
    switch (p.mode) { case M_OCT: ratio = 2.f; break; case M_5TH: ratio = 1.5f; break;
      case M_2OCT: ratio = 4.f; break; case M_DUST: ratio = 2.f; break; }
    float spread = (p.mode == M_DUST) ? 1.f : 0.5f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 0.7f, p.grain, 1.f, spread, 0.5f, false, gl, gr);
      float grainMono = (gl + gr) * 0.5f;
      float pitched = ps_.process(grainMono + fb_ * 0.5f, ratio);
      float feed = grainMono + pitched * p.shimmer * 0.8f;
      float rl, rr; verb_.process(feed, feed, rl, rr, 0.8f, 0.3f);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::PitchShifter ps_; dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f; float fb_ = 0.f;
};
