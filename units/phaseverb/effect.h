#pragma once
// PhaseVerb — phaser -> reverb.
// X=RATE Y=FEEDBACK DEPTH=mix MODE=WARM/JET/DEEP/WASH
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_WARM = 0, M_JET, M_DEEP, M_WASH, NUM_MODES };
  struct Params { float rate = 0.3f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_WARM; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case RATE: params_.rate = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WARM", "JET", "DEEP", "WASH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); verb_.init(alloc_);
    for (int i = 0; i < kAP; ++i) z_[i] = 0.f; fb_ = 0.f; params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { for (int i = 0; i < kAP; ++i) z_[i] = 0.f; fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    int stages = kAP; float rateScale = 1.f, verbAmt = 0.4f;
    switch (p.mode) { case M_WARM: stages = 4; break; case M_JET: stages = 6; rateScale = 2.f; break;
      case M_DEEP: stages = 8; break; case M_WASH: stages = 6; verbAmt = 0.9f; break; }
    const float inc = (0.05f + p.rate * 3.f) * rateScale / dsp::kSampleRate;
    const float fbAmt = p.feedback * 0.9f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float g = 0.5f + 0.45f * lfo_.next(inc);
      float x = dry + fb_ * fbAmt;
      for (int i = 0; i < stages; ++i) { float y = -g * x + z_[i]; z_[i] = x + g * y; x = y; }
      fb_ = x;
      float rl, rr; verb_.process(x, x, rl, rr, 0.75f, 0.35f);
      float wl = x + rl * verbAmt, wr = x + rr * verbAmt;
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix); out[1] = dsp::driveMix(in[1], mDrive_, wr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  static constexpr int kAP = 8;
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; dsp::LFO lfo_; float z_[kAP] = {0.f}; float fb_ = 0.f; Params params_; float mDrive_ = 0.f;
};
