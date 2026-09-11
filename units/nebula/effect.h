#pragma once
// Nebula — ambient delay into a reverb wash (free time, not synced).
// X=TIME (100 ms .. 1 s)  Y=FEEDBACK  DEPTH=mix  MODE=SOFT/GLASS/DARK/INF  DRIVE=grit
//   SOFT   diffuse, gently ping-ponged echoes melting into a hall
//   GLASS  bright crystalline echoes, thin lows, short airy verb
//   DARK   heavily damped echoes sinking into a dark cavern
//   INF    runaway feedback into a 25 s tail: an endless cloud
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_GLASS, M_DARK, M_INF, NUM_MODES };
  struct Params { float time = 0.5f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "GLASS", "DARK", "INF"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 1.05f, false);
    verb_.init(alloc_, 2.4f, 0.f);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_SOFT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 1.084f, 0.891f, 1.148f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   fbMax  cross hiCut loCut  wow  rSize rDecay rDamp send  trim
    static const float kM[NUM_MODES][10] = {
      {0.85f, 0.25f, 0.40f, 0.10f, 3.f, 1.4f,  3.5f, 0.40f, 0.70f, 0.90f},  // SOFT
      {0.85f, 0.50f, 0.05f, 0.45f, 0.f, 0.9f,  2.5f, 0.05f, 0.50f, 0.90f},  // GLASS
      {0.90f, 0.10f, 0.75f, 0.05f, 2.f, 1.8f,  5.0f, 0.80f, 0.80f, 1.00f},  // DARK
      {1.10f, 0.30f, 0.30f, 0.15f, 4.f, 2.2f, 25.0f, 0.35f, 1.00f, 0.75f},  // INF
    };
    const float *m = kM[mode];
    const float t = 4800.f + p.time * p.time * 43200.f;
    dsp::StereoDelay::Config d;
    d.timeL = t; d.timeR = t * 0.75f;
    d.feedback = p.feedback * m[0];
    d.cross = m[1]; d.hiCut = m[2]; d.loCut = m[3]; d.wow = m[4]; d.glide = 0.0005f;
    dly_.setConfig(d);
    dsp::Reverb::Config c;
    c.size = m[5]; c.decay = m[6]; c.damp = m[7]; c.diffusion = 0.8f; c.modDepth = 8.f;
    verb_.setConfig(c);
    const float send = m[8], trim = m[9];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dl, dr; dly_.process(in[0], in[1], dl, dr);
      float rl, rr; verb_.process(dl, dr, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, dl + rl * send, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, dr + rr * send, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::StereoDelay dly_;
  dsp::Reverb verb_;
  Params params_;
  float mDrive_ = 0.f;
};
