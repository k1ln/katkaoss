#pragma once
// HallVerb — concert halls.
// X=SIZE  Y=TONE  DEPTH=mix  MODE=SMALL/MED/LARGE/EPIC  DRIVE=grit
//
// The four modes are separate *spaces*, not one space at four decay times:
// each sets its own delay-length scale, pre-delay, diffusion and tail
// modulation. Pre-delay is what makes a hall read as large before the tail
// even arrives, so it scales hard across the modes.
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SMALL = 0, M_MED, M_LARGE, M_EPIC, NUM_MODES };
  struct Params { float size = 0.6f, tone = 0.5f, depth = 0.f; uint32_t mode = M_MED; };

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
    static const char *m[NUM_MODES] = {"SMALL", "MED", "LARGE", "EPIC"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 3.4f, 0.12f);   // up to 3.4x room scale, 120ms pre-delay
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    //                       size  decay  preDelay  diffusion  mod   width
    static const float kM[NUM_MODES][6] = {
      {0.70f,  1.1f, 0.008f, 0.60f,  3.0f, 0.70f},   // SMALL   recital room
      {1.20f,  2.4f, 0.026f, 0.70f,  5.0f, 0.85f},   // MED     concert hall
      {2.00f,  5.5f, 0.045f, 0.76f,  9.0f, 1.00f},   // LARGE   arena
      {3.20f, 14.0f, 0.090f, 0.82f, 16.0f, 1.00f},   // EPIC    canyon
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_MED];

    dsp::Reverb::Config c;
    // X trims the mode's space by +/- ~35% and stretches the tail with it.
    c.size = m[0] * (0.65f + p.size * 0.7f);
    c.decay = m[1] * (0.35f + p.size * 1.3f);
    c.preDelay = m[2] * (0.3f + p.size);
    c.diffusion = m[3];
    c.modDepth = m[4];
    c.modRate = 0.35f + (float)p.mode * 0.12f;
    c.width = m[5];
    c.damp = 1.f - p.tone;          // Y fully open = bright hall
    c.lowCut = 0.12f;
    verb_.setConfig(c);

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 1.000f, 0.912f};  // @auto-level

    const float lvl = kLevel[(p.mode < NUM_MODES ? p.mode : 0)];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
