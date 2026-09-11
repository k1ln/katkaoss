#pragma once
// Shimmer — the long, lush one: a cathedral whose tail is pitch-shifted and
// fed back in, so each pass climbs (or sinks) another interval.
// X=SIZE  Y=SHIMMER (pitched feedback)  DEPTH=mix  INTERVAL=OCT+/5TH/OCT-/DUAL  DRIVE=grit
//   OCT+  octave-up halo      5TH   fifth-up, brassy-bright
//   OCT-  sinks into a sub bloom   DUAL  octave + fifth together: organ-like
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, SHIMMER, DEPTH, INTERVAL, NUM_PARAMS };
  enum { I_OCTUP = 0, I_FIFTH, I_OCTDOWN, I_DUAL, NUM_MODES };
  struct Params { float size = 0.6f, shimmer = 0.5f, depth = 0.f; uint32_t interval = I_OCTUP; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case SHIMMER: params_.shimmer = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case INTERVAL: params_.interval = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"OCT+", "5TH", "OCT-", "DUAL"};
    if (i == INTERVAL && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 2.6f, 0.04f);
    ps1_.init(alloc_.alloc(7208), 7208);
    ps2_.init(alloc_.alloc(7208), 7208);
    fb_ = 0.f; hp_ = 0.f; lp_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.interval < NUM_MODES ? p.interval : I_OCTUP;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.813f, 0.804f, 0.832f, 0.822f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    ratio1 ratio2  hpCoef  lpCoef  damp
    static const float kM[NUM_MODES][5] = {
      {2.0f, 0.0f, 0.020f, 0.60f, 0.25f},  // OCT+
      {1.5f, 0.0f, 0.020f, 0.55f, 0.20f},  // 5TH
      {0.5f, 0.0f, 0.001f, 0.10f, 0.45f},  // OCT-
      {2.0f, 1.5f, 0.020f, 0.55f, 0.25f},  // DUAL
    };
    const float *m = kM[mode];
    dsp::Reverb::Config c;
    c.size = 1.4f + p.size * 1.2f;
    c.decay = 4.f + p.size * 8.f;
    c.preDelay = 0.02f; c.diffusion = 0.8f; c.modDepth = 9.f; c.modRate = 0.4f;
    c.damp = m[4]; c.lowCut = 0.1f;
    verb_.setConfig(c);
    const float shim = p.shimmer * 0.75f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      // pitch the tail, band-limit it (keeps the climb from turning to mud or
      // fizz), and feed it back in under a soft limiter so it can never run away
      float pitched = ps1_.process(fb_, m[0]);
      if (m[1] > 0.f) pitched = 0.6f * (pitched + ps2_.process(fb_, m[1]));
      hp_ += m[2] * (pitched - hp_); pitched -= hp_;
      lp_ += m[3] * (pitched - lp_); pitched = lp_;
      const float feed = dry + dsp::softLimit(pitched * shim);
      float rl, rr; verb_.process(feed, feed, rl, rr);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::PitchShifter ps1_, ps2_;
  float fb_ = 0.f, hp_ = 0.f, lp_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
