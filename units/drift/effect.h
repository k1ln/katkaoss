#pragma once
// Drift — flanger / phaser / jet modulation.
// X=RATE  Y=FEEDBACK  DEPTH=mix  MODE=FLANGER/PHASER/CHORUS/JET
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_FLANGER = 0, M_PHASER, M_CHORUS, M_JET, NUM_MODES };

  struct Params { float rate = 0.3f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_FLANGER; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FLANGER", "PHASER", "CHORUS", "JET"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dl_.init(alloc_.alloc(4096), 4096);
    for (int i = 0; i < kAP; ++i) apz_[i] = 0.f;
    fb_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; for (int i = 0; i < kAP; ++i) apz_[i] = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const float inc = (0.03f + p.rate * (p.mode == M_JET ? 6.f : 2.f)) / dsp::kSampleRate;
    const float fbAmt = p.feedback * 0.9f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float lfo = lfo_.next(inc);
      float wet;
      if (p.mode == M_PHASER) {
        float g = 0.5f + 0.45f * lfo;
        float x = dry + fb_ * fbAmt;
        for (int i = 0; i < kAP; ++i) {
          float y = -g * x + apz_[i];
          apz_[i] = x + g * y;
          x = y;
        }
        wet = x;
        fb_ = wet;
      } else {
        float baseD = (p.mode == M_CHORUS) ? 700.f : 120.f;
        float modD = (p.mode == M_CHORUS) ? 400.f : 100.f;
        float d = baseD + (lfo * 0.5f + 0.5f) * modD;
        dl_.write(dry + fb_ * fbAmt);
        wet = dl_.read(d);
        fb_ = wet;
      }
      out[0] = dsp::driveMix(in[0], mDrive_, wet, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, wet, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  static constexpr int kAP = 6;
  dsp::BufferAllocator alloc_;
  dsp::DelayLine dl_;
  dsp::LFO lfo_;
  float apz_[kAP] = {0.f};
  float fb_ = 0.f;
  Params params_; float mDrive_ = 0.f;
};
