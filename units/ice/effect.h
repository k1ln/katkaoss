#pragma once
// Ice — short, glassy, bright: the pitched sparkle sits *on top* of a small
// crystalline plate instead of recirculating (for long pitch cascades see
// Shimmer). Thin lows, no damping, fast decay.
// X=TUNE  Y=GLISTEN (sparkle level + plate length)  DEPTH=mix  MODE=OCT+/2OCT/5TH/DETUNE  DRIVE=grit
//   OCT+ / 2OCT / 5TH  pitched sparkle (X detunes it +/-25%)
//   DETUNE  two shifted copies a few cents apart: icy chorus
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
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
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
    verb_.init(alloc_, 1.0f, 0.f);
    psL_.init(alloc_.alloc(7208), 7208);
    psR_.init(alloc_.alloc(7208), 7208);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_OCT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.00f, 1.00f, 1.00f, 1.00f};  // @auto-level
    const float lvl = kLevel[mode];
    float ratio = 2.f, spreadR = 1.f;
    switch (mode) {
      case M_OCT: ratio = 2.f; break;
      case M_2OCT: ratio = 4.f; break;
      case M_5TH: ratio = 1.5f; break;
      default: ratio = 1.f + (0.004f + p.tune * 0.03f); spreadR = -1.f; break;  // +/- cents
    }
    if (mode != M_DETUNE) ratio *= (0.75f + p.tune * 0.5f);
    const float ratioR = spreadR > 0.f ? ratio * 1.003f : 2.f - ratio;  // L/R slightly apart
    dsp::Reverb::Config c;
    c.size = 0.55f + p.glisten * 0.35f;
    c.decay = 0.8f + p.glisten * 1.8f;
    c.damp = 0.02f; c.lowCut = 0.6f; c.diffusion = 0.85f; c.modDepth = 2.f; c.width = 1.f;
    verb_.setConfig(c);
    const float spark = 0.4f + p.glisten * 0.6f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      const float sl = psL_.process(dry, ratio), sr = psR_.process(dry, ratioR);
      float rl, rr; verb_.process(dry + sl * spark, dry + sr * spark, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, rl * 0.8f + sl * spark * 0.5f, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr * 0.8f + sr * spark * 0.5f, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::PitchShifter psL_, psR_;
  Params params_;
  float mDrive_ = 0.f;
};
