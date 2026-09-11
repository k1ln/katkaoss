#pragma once
// Nimbus — always-long, soft grains blooming into a wide space (for
// percussive / variable grains see Clouds).
// X=POSITION  Y=DENSITY  DEPTH=mix  MODE=SOFT/DENSE/FREEZE/BLOOM  DRIVE=grit
//   SOFT    slow soft swells       DENSE  thicker, slightly detuned
//   FREEZE  holds the sound (waits for real input) as a soft pad
//   BLOOM   the longest grains into an 8 s bloom
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
  enum { POSITION = 0U, DENSITY, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_DENSE, M_FREEZE, M_BLOOM, NUM_MODES };
  struct Params { float position = 0.5f, density = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case POSITION: params_.position = param_10bit_to_f32(v); break;
      case DENSITY: params_.density = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "DENSE", "FREEZE", "BLOOM"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 2.0f, 0.03f);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_SOFT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.684f, 0.716f, 0.631f, 0.550f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.900f, 0.f, 0.f, 0.f, 0.300f, 0.400f, 0.800f},
      {0.800f, 0.120f, 0.f, 0.f, 0.300f, 0.300f, 1.000f},
      {0.900f, 0.f, 0.f, 0.f, 0.200f, 0.300f, 0.900f},
      {0.950f, 0.200f, 0.f, 0.f, 0.400f, 0.500f, 1.000f},
    };
    static const float kV[NUM_MODES][8] = {
      {1.400f, 3.500f, 0.020f, 0.400f, 8.000f, 0.800f, 1.000f, 0.600f},
      {1.500f, 3.500f, 0.020f, 0.400f, 8.000f, 0.800f, 1.000f, 0.600f},
      {1.700f, 6.000f, 0.020f, 0.350f, 8.000f, 0.800f, 1.000f, 0.800f},
      {1.900f, 8.000f, 0.030f, 0.300f, 10.000f, 0.820f, 0.900f, 1.000f},
    };
    static const float kSize[NUM_MODES] = {0.65f, 0.55f, 0.70f, 0.85f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (1.f); c.decay = v[1] * (1.f);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, p.density, kSize[mode], 1.f, 0.6f, p.position, mode == M_FREEZE, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * v[6] + rl * v[7], mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr * v[6] + rr * v[7], mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::Reverb verb_;
  dsp::GrainKnobs knobs_;
  Params params_;
  float mDrive_ = 0.f;
};
