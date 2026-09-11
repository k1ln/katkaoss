#pragma once
// Ice — crystalline pitch-shifted reverb with glistening modulation.
// X=TUNE  Y=GLISTEN  DEPTH=mix  MODE=OCT+/2OCT/5TH/DETUNE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TUNE = 0U, GLISTEN, DEPTH, MODE, NUM_PARAMS };
  enum { M_OCT = 0, M_2OCT, M_5TH, M_DETUNE, NUM_MODES };

  struct Params { float tune = 0.5f, glisten = 0.5f, depth = 0.f; uint32_t mode = M_OCT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case TUNE: params_.tune = param_10bit_to_f32(v); break;
      case GLISTEN: params_.glisten = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"OCT+", "2OCT", "5TH", "DETUNE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_);
    ps_.init(alloc_.alloc(24000), 24000);
    params_ = Params();
    fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float ratio = 2.f;
    switch (p.mode) {
      case M_OCT: ratio = 2.f; break;
      case M_2OCT: ratio = 4.f; break;
      case M_5TH: ratio = 1.5f; break;
      case M_DETUNE: ratio = 1.f + p.tune * 0.03f; break;
    }
    if (p.mode != M_DETUNE) ratio *= (0.75f + p.tune * 0.5f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float sparkle = ps_.process(dry + fb_ * 0.5f, ratio);
      float rl, rr;
      verb_.process(dry + sparkle * p.glisten, dry + sparkle * p.glisten * 0.8f, rl, rr,
                    0.7f + p.glisten * 0.28f, 0.2f);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl + sparkle * 0.3f, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, rr + sparkle * 0.3f, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::PitchShifter ps_;
  Params params_; float mDrive_ = 0.f;
  float fb_ = 0.f;
};
