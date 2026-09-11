#pragma once
// Frostbite — icy, octave-up reverse grains in a bright, thin space.
// X=FREEZE (position; past halfway it holds the sound)  Y=SIZE  DEPTH=mix
// MODE=FROST/CRACK/BLIZZARD/THAW  DRIVE=grit
//   FROST     half the grains backwards, glassy     CRACK  all backwards, percussive cracks
//   BLIZZARD  dense, octave-jumping, scattered       THAW   unpitched, soft, warming space (never holds)
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
  enum { FREEZE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_FROST = 0, M_CRACK, M_BLIZZARD, M_THAW, NUM_MODES };
  struct Params { float freeze = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_FROST; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case FREEZE: params_.freeze = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FROST", "CRACK", "BLIZZARD", "THAW"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 2.0f, 0.0f);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_FROST;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 1.514f, 1.175f, 0.804f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.500f, 0.f, 0.f, 0.500f, 0.200f, 0.300f, 1.000f},
      {0.050f, 0.f, 0.f, 1.000f, 0.300f, 1.000f, 1.000f},
      {0.300f, 12.f, 2.f, 0.500f, 0.800f, 0.800f, 1.000f},
      {0.900f, 0.f, 0.f, 0.000f, 0.300f, 0.300f, 0.700f},
    };
    static const float kV[NUM_MODES][8] = {
      {1.200f, 2.500f, 0.000f, 0.050f, 3.000f, 0.850f, 0.400f, 1.000f},
      {0.900f, 1.800f, 0.000f, 0.020f, 2.000f, 0.600f, 0.600f, 0.800f},
      {1.600f, 4.000f, 0.000f, 0.100f, 6.000f, 0.850f, 0.400f, 1.000f},
      {1.400f, 3.500f, 0.000f, 0.500f, 5.000f, 0.800f, 0.500f, 0.900f},
    };
    static const float kPitch[NUM_MODES] = {2.f, 2.f, 2.f, 1.f}, kDens[NUM_MODES] = {0.5f, 0.4f, 1.f, 0.3f};
    const bool frz = (mode != M_THAW) && p.freeze > 0.5f;
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (1.f); c.decay = v[1] * (0.6f + p.size * 1.2f);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.size, kPitch[mode], 0.8f, p.freeze, frz, gl, gr);
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
