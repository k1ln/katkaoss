#pragma once
// GateVerb — 80s gated reverb and friends.
// X=SIZE  Y=GATE (hold time)  DEPTH=mix  MODE=GATE/REV/DUCK/SLAM  DRIVE=grit
//   GATE  big dense room chopped off after the hold — the snare classic
//   REV   the gate ramps *up* over the hold: a backwards-sounding swell
//   DUCK  no gate: a long hall that ducks under the input and blooms in the gaps
//   SLAM  short hold, slammed through saturation, bright
// (DUCK is quiet by design while signal is playing — that is the effect.)
// @level-exempt 2
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, GATE, DEPTH, MODE, NUM_PARAMS };
  enum { M_GATE = 0, M_REV, M_DUCK, M_SLAM, NUM_MODES };
  struct Params { float size = 0.6f, gate = 0.5f, depth = 0.f; uint32_t mode = M_GATE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case GATE: params_.gate = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"GATE", "REV", "DUCK", "SLAM"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 2.2f, 0.03f);
    env_ = 0.f; hold_ = 0; gateGain_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_GATE;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.912f, 0.813f, 1.000f, 0.638f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    size  decay  pre     diff  damp  holdMul  trim
    static const float kM[NUM_MODES][7] = {
      {1.20f, 2.5f, 0.000f, 0.85f, 0.30f, 1.00f, 1.00f},  // GATE
      {1.40f, 3.0f, 0.020f, 0.80f, 0.35f, 1.30f, 1.10f},  // REV
      {1.80f, 4.5f, 0.025f, 0.78f, 0.40f, 1.00f, 1.00f},  // DUCK
      {0.90f, 1.8f, 0.000f, 0.85f, 0.10f, 0.45f, 0.70f},  // SLAM
    };
    const float *m = kM[mode];
    dsp::Reverb::Config c;
    c.size = m[0] * (0.7f + p.size * 0.6f);
    c.decay = m[1]; c.preDelay = m[2]; c.diffusion = m[3]; c.damp = m[4];
    c.modDepth = 2.f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    const uint32_t holdLen = (uint32_t)((1200.f + p.gate * 14400.f) * m[5]);  // 25..320 ms
    const bool rev = (mode == M_REV), duck = (mode == M_DUCK), slam = (mode == M_SLAM);
    const float trim = m[6];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      env_ += 0.01f * (fabsf(dry) - env_);
      if (env_ > 0.02f) hold_ = holdLen;   // (re)trigger on input
      const float target = hold_ > 0 ? 1.f : 0.f;
      if (hold_ > 0) --hold_;
      gateGain_ += (rev ? 0.0006f : 0.02f) * (target - gateGain_);
      float rl, rr; verb_.process(in[0], in[1], rl, rr);
      float g = duck ? 1.f - dsp::clampf(env_ * 8.f, 0.f, 0.8f) : gateGain_;
      float wl = rl * g, wr = rr * g;
      if (slam) { wl = dsp::softclip(wl * 2.5f); wr = dsp::softclip(wr * 2.5f); }
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, wr, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  float env_ = 0.f, gateGain_ = 0.f;
  uint32_t hold_ = 0;
  Params params_;
  float mDrive_ = 0.f;
};
