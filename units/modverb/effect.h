#pragma once
// ModVerb — modulated reverb: the tail itself moves.
// X=SIZE  Y=MODRATE  DEPTH=mix  MODE=SOFT/LUSH/SEASICK/WOW  DRIVE=grit
// Both the tank's delay lines and a stereo pre-delay (L and R modulated in
// opposite directions) are swept, so modulation also widens the image.
//   SOFT     gentle shimmer of movement    LUSH  deep chorused hall
//   SEASICK  slow, deep pitch sway          WOW   tape-wow wobble into a warm room
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, MODRATE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_LUSH, M_SEASICK, M_WOW, NUM_MODES };
  struct Params { float size = 0.6f, modrate = 0.4f, depth = 0.f; uint32_t mode = M_LUSH; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case MODRATE: params_.modrate = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "LUSH", "SEASICK", "WOW"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    pre_.init(alloc_.alloc(8192), 8192);
    verb_.init(alloc_, 2.2f, 0.f);
    ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_LUSH;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 1.135f, 1.318f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    tankMod rateLo rateHi preMod  size  decay damp
    static const float kM[NUM_MODES][7] = {
      {12.f, 0.30f, 2.0f,  40.f, 1.40f, 3.2f, 0.30f},  // SOFT
      {18.f, 0.20f, 1.5f,  60.f, 1.60f, 3.6f, 0.30f},  // LUSH
      {45.f, 0.06f, 0.6f, 240.f, 1.40f, 4.0f, 0.40f},  // SEASICK
      {30.f, 0.05f, 0.4f, 420.f, 1.00f, 2.6f, 0.55f},  // WOW
    };
    const float *m = kM[mode];
    const float rate = m[1] + p.modrate * (m[2] - m[1]);
    dsp::Reverb::Config c;
    c.size = m[4] * (0.7f + p.size * 0.6f);
    c.decay = m[5] * (0.5f + p.size);
    c.modDepth = m[0]; c.modRate = rate; c.damp = m[6];
    c.diffusion = 0.74f; c.preDelay = 0.f; c.lowCut = 0.1f;
    verb_.setConfig(c);
    const float inc = rate / dsp::kSampleRate, depthMod = m[3];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      pre_.write(dry);
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      const float s = dsp::fastSin01(ph_) * 0.5f;
      const float cl = pre_.read(600.f + (0.5f + s) * depthMod);
      const float cr = pre_.read(600.f + (0.5f - s) * depthMod);
      float rl, rr; verb_.process(cl, cr, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine pre_;
  dsp::Reverb verb_;
  float ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
