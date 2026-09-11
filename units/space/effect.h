#pragma once
// Space — the all-round reverb: four distinct kinds of space.
// X=SIZE  Y=TONE  DEPTH=mix  TYPE=ROOM/HALL/PLATE/VAST  DRIVE=grit
//   ROOM   small, early-reflection heavy, sub-second
//   HALL   medium hall with a clear pre-delay gap
//   PLATE  no pre-delay, maximum density, bright with thin lows
//   VAST   huge, slow-blooming, dark and endless
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, TYPE, NUM_PARAMS };
  enum { T_ROOM = 0, T_HALL, T_PLATE, T_VAST, NUM_MODES };
  struct Params { float size = 0.5f, tone = 0.5f, depth = 0.f; uint32_t type = T_HALL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case TYPE: params_.type = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"ROOM", "HALL", "PLATE", "VAST"};
    if (i == TYPE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 3.4f, 0.08f);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.type < NUM_MODES ? p.type : T_HALL;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 0.912f, 1.000f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];
    //                      size decay  pre     diff  mod   width lowCut dampMul
    static const float kM[NUM_MODES][8] = {
      {0.75f,  0.9f, 0.006f, 0.62f,  2.f, 0.80f, 0.10f, 1.0f},  // ROOM
      {1.80f,  4.2f, 0.035f, 0.76f,  7.f, 1.00f, 0.10f, 0.8f},  // HALL
      {0.95f,  2.4f, 0.000f, 0.85f,  3.f, 1.00f, 0.40f, 0.5f},  // PLATE
      {3.20f, 16.0f, 0.070f, 0.82f, 14.f, 1.00f, 0.05f, 1.3f},  // VAST
    };
    const float *m = kM[mode];
    dsp::Reverb::Config c;
    c.size = m[0] * (0.7f + p.size * 0.6f);
    c.decay = m[1] * (0.45f + p.size * 1.1f);
    c.preDelay = m[2]; c.diffusion = m[3]; c.modDepth = m[4]; c.modRate = 0.5f;
    c.width = m[5]; c.lowCut = m[6];
    c.damp = dsp::clampf((1.f - p.tone) * m[7], 0.f, 1.f);
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  Params params_;
  float mDrive_ = 0.f;
};
