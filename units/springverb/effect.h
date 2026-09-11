#pragma once
// SpringVerb — dispersive spring tank.
// X=TENSION  Y=TONE  DEPTH=mix  MODE=1SPR/2SPR/3SPR/DRIP  DRIVE=grit
//
// A spring does not sound like a room: the boing comes from *dispersion* —
// high frequencies travelling down the wire faster than low ones — not from
// diffuse decay. So the character here is a long cascade of first-order
// allpasses (frequency-dependent delay) *inside* the tank's feedback loop, so
// every recirculation smears further, around a deliberately small, bright,
// under-diffused tank. Low diffusion is the point: springs flutter.
// A pre-disperser on the input adds the initial "drip" transient.
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TENSION = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_1 = 0, M_2, M_3, M_DRIP, NUM_MODES };
  struct Params { float tension = 0.5f, tone = 0.5f, depth = 0.f; uint32_t mode = M_2; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case TENSION: params_.tension = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"1SPR", "2SPR", "3SPR", "DRIP"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    verb_.init(alloc_, 1.2f, 0.f);      // springs are small and have no pre-delay
    disp_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { disp_.reset(); }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    //                  loopStg  disp  preStg  size  decay  width
    static const float kM[NUM_MODES][6] = {
      { 6.f, 0.60f,  4.f, 0.38f, 1.2f, 0.35f},   // 1SPR  single short spring
      {12.f, 0.68f,  8.f, 0.46f, 1.8f, 0.50f},   // 2SPR  classic amp tank
      {20.f, 0.74f, 12.f, 0.55f, 2.6f, 0.70f},   // 3SPR  long triple tank
      {24.f, 0.86f, 24.f, 0.30f, 1.4f, 0.25f},   // DRIP  over-tensioned, splashy
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_2];

    // TENSION tightens the wire: more dispersion and a longer ring.
    const float t = 0.5f + p.tension * 0.8f;
    const float g = dsp::clampf(m[1] + p.tension * 0.07f, 0.f, 0.93f);
    preStages_ = (int)(m[2] * t);
    preCoef_ = g;

    dsp::Reverb::Config c;
    c.dispStages = (int)(m[0] * t);
    c.dispCoef = g;
    c.size = m[3];
    c.decay = m[4] * (0.6f + p.tension * 0.8f);
    c.diffusion = 0.32f;     // deliberately under-diffused -> spring flutter
    c.damp = 0.62f - p.tone * 0.5f;
    c.lowCut = 0.75f;        // spring tanks have no bottom end
    c.modDepth = 1.5f;
    c.modRate = 1.8f;
    c.width = m[5];
    verb_.setConfig(c);

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_MODES] = {1.000f, 1.122f, 1.175f, 1.349f};  // @auto-level

    const float lvl = kLevel[(p.mode < NUM_MODES ? p.mode : 0)];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      const float x = disp_.process(dry, preCoef_, preStages_);  // initial drip
      float rl, rr; verb_.process(x, x, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_;
  dsp::Disperser<32> disp_;
  dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f;
  int preStages_ = 0; float preCoef_ = 0.7f;
};
