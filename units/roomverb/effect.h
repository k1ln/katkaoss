#pragma once
// RoomVerb — small real rooms. Low diffusion on purpose: the early
// reflections stay audible as separate slaps, which is what makes a room read
// as a room rather than a short hall.
// X=SIZE  Y=TONE  DEPTH=mix  MODE=TIGHT/WOOD/TILE/BOOTH  DRIVE=grit
//   TIGHT  neutral small room      WOOD  warm, soft-walled
//   TILE   bright, slappy bathroom  BOOTH tiny and nearly dead
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_TIGHT = 0, M_WOOD, M_TILE, M_BOOTH, NUM_MODES };
  struct Params { float size = 0.4f, tone = 0.5f, depth = 0.f; uint32_t mode = M_TIGHT; };

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
    static const char *m[NUM_MODES] = {"TIGHT", "WOOD", "TILE", "BOOTH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 1.0f, 0.f);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_TIGHT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.202f, 1.189f, 1.000f, 1.820f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    size  decay  pre     diff  width dampAdd
    static const float kM[NUM_MODES][6] = {
      {0.40f, 0.40f, 0.002f, 0.55f, 0.60f,  0.00f},  // TIGHT
      {0.60f, 0.85f, 0.004f, 0.60f, 0.85f,  0.35f},  // WOOD
      {0.55f, 1.20f, 0.003f, 0.38f, 0.90f, -0.40f},  // TILE
      {0.25f, 0.22f, 0.001f, 0.50f, 0.40f,  0.55f},  // BOOTH
    };
    const float *m = kM[mode];
    dsp::Reverb::Config c;
    c.size = m[0] * (0.7f + p.size * 0.6f);
    c.decay = m[1] * (0.5f + p.size);
    c.preDelay = m[2]; c.diffusion = m[3]; c.width = m[4];
    c.modDepth = 0.5f; c.lowCut = 0.15f;
    c.damp = dsp::clampf(1.f - p.tone + m[5], 0.f, 1.f);
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
