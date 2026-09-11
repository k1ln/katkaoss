#pragma once
// RingVerb — ring modulator into a space.
// X=FREQ  Y=SIZE  DEPTH=mix  MODE=BELL/METAL/ALIEN/SUB  DRIVE=grit
//   BELL   bell partials into a bright plate
//   METAL  clangy, under-diffused tight room that rings
//   ALIEN  wobbling carrier into a huge modulated space
//   SUB    very low carrier (tremolo-ish) into a dark hall
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { FREQ = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_BELL = 0, M_METAL, M_ALIEN, M_SUB, NUM_MODES };
  struct Params { float freq = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_BELL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case FREQ: params_.freq = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"BELL", "METAL", "ALIEN", "SUB"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 2.4f, 0.f);
    osc_.reset(); wob_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_BELL;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.175f, 1.216f, 1.000f, 1.161f};  // @auto-level
    const float lvl = kLevel[mode];
    //                    lo     hi    size  decay diff  damp  mod  wobble ringLvl send
    static const float kM[NUM_MODES][10] = {
      {200.f, 1200.f, 0.9f, 3.5f, 0.85f, 0.15f,  3.f, 0.00f, 0.6f, 0.8f},  // BELL
      {400.f, 3000.f, 0.5f, 1.2f, 0.35f, 0.00f,  0.f, 0.00f, 0.8f, 0.6f},  // METAL
      { 50.f,  800.f, 2.2f, 6.0f, 0.80f, 0.30f, 25.f, 0.35f, 0.5f, 1.0f},  // ALIEN
      { 20.f,  200.f, 1.6f, 4.5f, 0.78f, 0.70f,  6.f, 0.00f, 0.6f, 0.9f},  // SUB
    };
    const float *m = kM[mode];
    const float hz = m[0] + p.freq * (m[1] - m[0]);
    dsp::Reverb::Config c;
    c.size = m[2] * (0.6f + p.size * 0.8f);
    c.decay = m[3] * (0.5f + p.size);
    c.diffusion = m[4]; c.damp = m[5]; c.modDepth = m[6]; c.modRate = 0.6f;
    verb_.setConfig(c);
    const float wobble = m[7], ringLvl = m[8], send = m[9];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      wob_ += 0.35f / dsp::kSampleRate; if (wob_ >= 1.f) wob_ -= 1.f;
      const float f = wobble > 0.f ? hz * (1.f + wobble * dsp::fastSin01(wob_)) : hz;
      const float ring = dry * osc_.sine(f);
      float rl, rr; verb_.process(ring, ring, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, ring * ringLvl + rl * send, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, ring * ringLvl + rr * send, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::Reverb verb_;
  dsp::Osc osc_;
  float wob_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
