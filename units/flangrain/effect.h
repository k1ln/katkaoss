#pragma once
// FlanGrain — flanger -> grain cloud.
// X=RATE Y=GRAIN DEPTH=mix MODE=SOFT/JET/METAL/CHAOS
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, GRAIN, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_JET, M_METAL, M_CHAOS, NUM_MODES };
  struct Params { float rate = 0.3f, grain = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case RATE: params_.rate = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "JET", "METAL", "CHAOS"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); fdl_.init(alloc_.alloc(4096), 4096);
    cloud_.init(alloc_.alloc(96000), 96000); params_ = Params(); fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float rateScale = 1.f, fbAmt = 0.6f, pitch = 1.f;
    switch (p.mode) { case M_SOFT: rateScale = 0.7f; fbAmt = 0.4f; break;
      case M_JET: rateScale = 2.f; fbAmt = 0.85f; break;
      case M_METAL: rateScale = 1.f; fbAmt = 0.9f; break;
      case M_CHAOS: rateScale = 3.f; fbAmt = 0.8f; pitch = 1.5f; break; }
    const float inc = (0.05f + p.rate * 3.f) * rateScale / dsp::kSampleRate;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float m = lfo_.next(inc) * 0.5f + 0.5f;
      fdl_.write(dry + fb_ * fbAmt);
      float flanged = fdl_.read(20.f + m * 300.f);  // short modulated delay
      fb_ = flanged;
      float gl, gr; cloud_.process(flanged, 0.6f, p.grain, pitch, 0.6f, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::DelayLine fdl_; dsp::LFO lfo_; dsp::GrainCloud cloud_; Params params_; float mDrive_ = 0.f; float fb_ = 0.f;
};
