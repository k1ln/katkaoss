#pragma once
// PhaseVerb — stereo phaser into a space. L and R sweep 90 degrees apart.
// X=RATE  Y=FEEDBACK (resonance)  DEPTH=mix  MODE=WARM/JET/DEEP/WASH  DRIVE=grit
//   WARM  4-stage, gentle, small warm room   JET   6-stage, fast, bright short room
//   DEEP  8-stage, deep notches, medium hall  WASH  6-stage into a huge long wash
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
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WARM", "JET", "DEEP", "WASH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 2.6f, 0.f);
    for (int c = 0; c < 2; ++c) { fb_[c] = 0.f; for (int i = 0; i < kAP; ++i) z_[c][i] = 0.f; }
    ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_WARM;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.109f, 1.259f, 1.175f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   stages rateMul rSize rDecay rDamp send  trim
    static const float kM[NUM_MODES][7] = {
      {4.f, 1.0f, 0.7f, 1.2f, 0.60f, 0.35f, 0.80f},  // WARM
      {6.f, 2.2f, 0.6f, 0.9f, 0.10f, 0.25f, 0.75f},  // JET
      {8.f, 0.7f, 1.3f, 2.8f, 0.35f, 0.50f, 0.75f},  // DEEP
      {6.f, 0.5f, 2.4f, 9.0f, 0.30f, 1.00f, 0.60f},  // WASH
    };
    const float *m = kM[mode];
    const int stages = (int)m[0];
    const float inc = (0.05f + p.rate * 3.f) * m[1] / dsp::kSampleRate;
    const float fbAmt = p.feedback * 0.85f;
    dsp::Reverb::Config c;
    c.size = m[2]; c.decay = m[3]; c.damp = m[4]; c.diffusion = 0.75f; c.modDepth = 3.f;
    verb_.setConfig(c);
    const float send = m[5], trim = m[6];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      float y[2];
      for (int ch = 0; ch < 2; ++ch) {
        float q = ph_ + (ch ? 0.25f : 0.f); if (q >= 1.f) q -= 1.f;
        const float g = 0.5f + 0.45f * dsp::fastSin01(q);
        float x = in[ch] + fb_[ch] * fbAmt;
        for (int i = 0; i < stages; ++i) { const float o = -g * x + z_[ch][i]; z_[ch][i] = x + g * o; x = o; }
        fb_[ch] = dsp::softLimit(x);
        y[ch] = (x + in[ch]) * 0.5f;   // classic phaser: notches come from summing with dry
      }
      float rl, rr; verb_.process(y[0], y[1], rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, y[0] + rl * send, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, y[1] + rr * send, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  static constexpr int kAP = 8;
  dsp::Reverb verb_;
  float z_[2][kAP] = {{0.f}}, fb_[2] = {0.f, 0.f}, ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
