#pragma once
// Clouds — granular texture feeding a reverb wash.
// X=TEXTURE (grain position + stereo spread)  Y=SIZE (grain size + tail length)
// DEPTH=mix  MODE=GRAIN/CLOUD/DENSE/FREEZE  DRIVE=grit
//   GRAIN   discrete percussive pebbles in a small room
//   CLOUD   drifting Hann grains in a hall
//   DENSE   a solid gated wall in a big space
//   FREEZE  holds what was playing (waits for real sound) in a cathedral
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
  enum { TEXTURE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_GRAIN = 0, M_CLOUD, M_DENSE, M_FREEZE, NUM_MODES };
  struct Params { float texture = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_CLOUD; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case TEXTURE: params_.texture = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"GRAIN", "CLOUD", "DENSE", "FREEZE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 2.2f, 0.06f);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_CLOUD;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.318f, 1.000f, 0.733f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.200f, 0.f, 0.f, 0.f, 0.300f, 0.500f, -1.000f},
      {0.500f, 0.f, 0.f, 0.f, 0.200f, 0.300f, -1.000f},
      {0.850f, 0.100f, 0.f, 0.f, 0.200f, 0.200f, -1.000f},
      {0.600f, 0.f, 0.f, 0.f, 0.200f, 0.200f, -1.000f},
    };
    static const float kV[NUM_MODES][8] = {
      {0.800f, 2.000f, 0.012f, 0.550f, 7.000f, 0.780f, 1.000f, 0.500f},
      {1.200f, 3.000f, 0.012f, 0.400f, 7.000f, 0.780f, 0.800f, 0.700f},
      {1.600f, 5.000f, 0.012f, 0.300f, 7.000f, 0.780f, 0.550f, 0.900f},
      {2.200f, 14.000f, 0.012f, 0.220f, 7.000f, 0.780f, 0.450f, 1.100f},
    };
    static const float kDens[NUM_MODES] = {0.30f, 0.70f, 1.00f, 0.85f}, kSpr[NUM_MODES] = {0.30f, 0.60f, 0.85f, 0.70f};
    const float spread = dsp::clampf(kSpr[mode] * (0.4f + p.texture), 0.f, 1.f);
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (1.f); c.decay = v[1] * (0.4f + p.size * 1.4f);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.size, 1.f, spread, p.texture, mode == M_FREEZE, gl, gr);
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
