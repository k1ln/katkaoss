#pragma once
// Vapor — vaporwave: slowed, pitched-down grains with tape wow into a space.
// X=SLOW (pitch-down amount)  Y=SIZE  DEPTH=mix  MODE=MALL/DREAM/SLOW/PLUSH  DRIVE=grit
//   MALL   a fourth down in an empty atrium    DREAM  an octave down, lush and modulated
//   SLOW   heavily slowed, dark                PLUSH  soft carpeted room, warm and close
//
// Extra knobs (NTS-3 edit menu, assignable to X/Y): SHAPE grain envelope
// (percussive <-> gated, centre = the mode's own), SCATTER per-grain
// pitch/size/timing randomness, REVERSE share of backwards grains.
// @param 5 SHAPE 0 1023 512
// @param 6 SCATTER 0 1023 0
// @param 7 REVERSE 0 1023 0
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SLOW = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_MALL = 0, M_DREAM, M_SLOW, M_PLUSH, NUM_MODES };
  struct Params { float slow = 0.5f, size = 0.6f, depth = 0.f; uint32_t mode = M_MALL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case SLOW: params_.slow = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"MALL", "DREAM", "SLOW", "PLUSH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    wow_.init(alloc_.alloc(4096), 4096);
    verb_.init(alloc_, 2.4f, 0.04f);
    knobs_.reset(); ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_MALL;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.841f, 0.813f, 0.750f, 0.881f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.80f, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.7f},  // MALL
      {0.85f, 0.f, 0.f, 0.f, 0.3f, 0.3f, 1.0f},  // DREAM
      {0.90f, 0.f, 0.f, 0.f, 0.3f, 0.3f, 0.6f},  // SLOW
      {0.85f, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.5f},  // PLUSH
    };
    //                   pitch  size  decay  pre    damp  mod
    static const float kV[NUM_MODES][6] = {
      {0.75f, 1.8f, 3.5f, 0.035f, 0.40f,  6.f},
      {0.50f, 2.2f, 6.0f, 0.020f, 0.30f, 14.f},
      {0.40f, 1.4f, 3.0f, 0.010f, 0.60f,  5.f},
      {0.60f, 0.8f, 1.6f, 0.004f, 0.70f,  3.f},
    };
    const float *v = kV[mode];
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float pitch = v[0] * (0.85f + p.slow * 0.3f);
    dsp::Reverb::Config c;
    c.size = v[1] * (0.7f + p.size * 0.5f); c.decay = v[2] * (0.5f + p.size);
    c.preDelay = v[3]; c.damp = v[4]; c.modDepth = v[5]; c.diffusion = 0.78f;
    verb_.setConfig(c);
    const float wowInc = 0.6f / dsp::kSampleRate;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, 0.6f, 0.5f + p.size * 0.5f, pitch, 0.5f, 0.5f, false, gl, gr);
      wow_.write((gl + gr) * 0.5f);
      ph_ += wowInc; if (ph_ >= 1.f) ph_ -= 1.f;
      const float w = wow_.read(300.f + (0.5f + 0.5f * dsp::fastSin01(ph_)) * 200.f);
      float rl, rr; verb_.process(w, w, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.4f + rl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.4f + rr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::DelayLine wow_;
  dsp::Reverb verb_;
  dsp::GrainKnobs knobs_;
  float ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
