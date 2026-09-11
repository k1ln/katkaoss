#pragma once
// PlateVerb — studio plate: no pre-delay, maximum diffusion, instant density.
// X=SIZE (decay)  Y=TONE  DEPTH=mix  MODE=STD/BRITE/DARK/WIDE  DRIVE=grit
//   STD    classic vocal plate          BRITE  thin, sizzling, lows cut
//   DARK   heavy, damped, long          WIDE   modulated plate with a Haas-widened right side
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_STD = 0, M_BRITE, M_DARK, M_WIDE, NUM_MODES };
  struct Params { float size = 0.55f, tone = 0.6f, depth = 0.f; uint32_t mode = M_STD; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"STD", "BRITE", "DARK", "WIDE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 1.3f, 0.f);
    haas_.init(alloc_.alloc(1024), 1024);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_STD;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 1.000f, 0.923f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    size  decay  diff  mod  lowCut dampMul dampAdd width
    static const float kM[NUM_MODES][8] = {
      {1.00f, 1.8f, 0.88f, 2.f, 0.35f, 0.55f, 0.00f, 0.90f},  // STD
      {0.90f, 2.6f, 0.86f, 2.f, 0.60f, 0.30f, 0.00f, 1.00f},  // BRITE
      {1.20f, 3.4f, 0.80f, 3.f, 0.10f, 1.00f, 0.35f, 0.80f},  // DARK
      {1.10f, 2.8f, 0.86f, 8.f, 0.25f, 0.80f, 0.00f, 1.00f},  // WIDE
    };
    const float *m = kM[mode];
    dsp::Reverb::Config c;
    c.size = m[0];
    c.decay = m[1] * (0.4f + p.size * 1.3f);
    c.diffusion = m[2]; c.modDepth = m[3]; c.lowCut = m[4]; c.width = m[7];
    c.damp = dsp::clampf((1.f - p.tone) * m[5] + m[6], 0.f, 1.f);
    verb_.setConfig(c);
    const bool wide = (mode == M_WIDE);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr);
      haas_.write(rr);
      if (wide) rr = haas_.read(530.f);   // 11 ms: pushes the image apart
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::DelayLine haas_;
  Params params_;
  float mDrive_ = 0.f;
};
