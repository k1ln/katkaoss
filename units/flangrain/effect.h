#pragma once
// FlanGrain — stereo flanger -> grain cloud.
// X=RATE  Y=GRAIN (size)  DEPTH=mix  MODE=SOFT/JET/METAL/CHAOS  DRIVE=grit
//   SOFT   slow gentle flange, smooth grains      JET    fast deep sweep, gated grains
//   METAL  high-feedback comb, tiny percussive grains: clangy
//   CHAOS  fast, a fifth up, random-pitch backwards grains
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
  enum { RATE = 0U, GRAIN, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_JET, M_METAL, M_CHAOS, NUM_MODES };
  struct Params { float rate = 0.3f, grain = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "JET", "METAL", "CHAOS"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    fdlL_.init(alloc_.alloc(2048), 2048);
    fdlR_.init(alloc_.alloc(2048), 2048);
    cloud_.init(alloc_.alloc(96000), 96000);
    knobs_.reset(); fbL_ = fbR_ = 0.f; ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_SOFT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.841f, 0.343f, 0.507f, 0.661f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.60f, 0.f, 0.f, 0.0f, 0.2f, 0.2f, 0.6f},  // SOFT
      {0.85f, 0.f, 0.f, 0.0f, 0.2f, 0.2f, 0.8f},  // JET
      {0.05f, 0.f, 0.f, 0.0f, 0.3f, 0.6f, 0.8f},  // METAL
      {0.35f, 7.f, 0.f, 0.5f, 0.7f, 0.9f, 1.0f},  // CHAOS
    };
    //                   rateMul fb    depth  pitch  gsizeMul
    static const float kF[NUM_MODES][5] = {
      {0.7f, 0.40f, 300.f, 1.0f, 1.0f},
      {2.0f, 0.80f, 360.f, 1.0f, 1.0f},
      {1.0f, 0.90f,  90.f, 1.0f, 0.3f},
      {3.0f, 0.75f, 300.f, 1.5f, 0.8f},
    };
    const float *f = kF[mode];
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float inc = (0.05f + p.rate * 3.f) * f[0] / dsp::kSampleRate;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      float q = ph_ + 0.25f; if (q >= 1.f) q -= 1.f;   // R sweeps 90 degrees behind L
      fdlL_.write(in[0] + dsp::softLimit(fbL_ * f[1]));
      fdlR_.write(in[1] + dsp::softLimit(fbR_ * f[1]));
      const float yl = fdlL_.read(20.f + (0.5f + 0.5f * dsp::fastSin01(ph_)) * f[2]);
      const float yr = fdlR_.read(20.f + (0.5f + 0.5f * dsp::fastSin01(q)) * f[2]);
      fbL_ = yl; fbR_ = yr;
      float gl, gr; cloud_.process((yl + yr) * 0.5f, 0.6f, p.grain * f[4], f[3], 0.6f, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl + yl * 0.3f, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr + yr * 0.3f, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine fdlL_, fdlR_;
  dsp::GrainCloud cloud_;
  dsp::GrainKnobs knobs_;
  float fbL_ = 0.f, fbR_ = 0.f, ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
