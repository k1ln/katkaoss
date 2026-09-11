#pragma once
// GateVerb — 80s gated reverb.
// X=SIZE Y=GATE(hold) DEPTH=mix MODE=GATE/REV/DUCK/SLAM
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
    switch (i) { case SIZE: params_.size = param_10bit_to_f32(v); break;
      case GATE: params_.gate = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"GATE", "REV", "DUCK", "SLAM"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); verb_.init(alloc_); params_ = Params();
    env_ = 0.f; hold_ = 0; gateGain_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { env_ = 0.f; hold_ = 0; gateGain_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t holdLen = (uint32_t)(1200.f + p.gate * 14400.f);  // 25..320ms
    const float mix = (p.depth + 1.f) * 0.5f;
    const float room = 0.5f + p.size * 0.3f;
    const bool rev = (p.mode == M_REV);
    const bool duck = (p.mode == M_DUCK);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float lvl = fabsf(dry);
      env_ += 0.01f * (lvl - env_);
      if (env_ > 0.02f) hold_ = holdLen;  // re-trigger gate on input
      float target = (hold_ > 0) ? 1.f : 0.f;
      if (hold_ > 0) hold_--;
      // REV mode ramps the gate up instead of holding flat
      float rate = rev ? 0.0006f : 0.02f;
      gateGain_ += rate * (target - gateGain_);
      float rl, rr; verb_.process(in[0], in[1], rl, rr, room, 0.3f);
      float g = gateGain_;
      float wl = rl * g, wr = rr * g;
      if (duck) { float d = 1.f - dsp::clampf(env_ * 8.f, 0.f, 0.8f); wl *= d; wr *= d; }
      if (p.mode == M_SLAM) { wl = dsp::softclip(wl * 2.f); wr = dsp::softclip(wr * 2.f); }
      out[0] = dsp::lerp(in[0], wl, mix); out[1] = dsp::lerp(in[1], wr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; Params params_;
  float env_ = 0.f; uint32_t hold_ = 0; float gateGain_ = 0.f;
};
