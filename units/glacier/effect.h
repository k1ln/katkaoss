#pragma once
// Glacier — slow, pitched-down, long grains sinking into an immense space.
// X=PITCH (fine, +/-20%)  Y=SIZE  DEPTH=mix  MODE=CALM/FLOW/CALVE/DEEP  DRIVE=grit
//   CALM   an octave down, smooth       FLOW   a fourth down, drifting and uneven
//   CALVE  two octaves down, percussive cracks, some reversed
//   DEEP   sub octaves below, into a 25 s dark tail
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
  enum { PITCH = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_CALM = 0, M_FLOW, M_CALVE, M_DEEP, NUM_MODES };
  struct Params { float pitch = 0.5f, size = 0.7f, depth = 0.f; uint32_t mode = M_CALM; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"CALM", "FLOW", "CALVE", "DEEP"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 3.3f, 0.05f);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_CALM;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.881f, 1.000f, 1.567f, 1.161f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.900f, 0.f, 0.f, 0.000f, 0.200f, 0.300f, 0.800f},
      {0.700f, 0.f, 0.f, 0.000f, 0.500f, 0.600f, 1.000f},
      {0.120f, 0.f, 0.f, 0.300f, 0.400f, 0.800f, 1.000f},
      {0.850f, 12.f, 2.f, 0.000f, 0.300f, 0.300f, 1.000f},
    };
    static const float kV[NUM_MODES][8] = {
      {2.600f, 10.000f, 0.040f, 0.550f, 6.000f, 0.800f, 0.350f, 1.000f},
      {2.200f, 7.000f, 0.030f, 0.450f, 9.000f, 0.800f, 0.350f, 1.000f},
      {3.000f, 12.000f, 0.050f, 0.400f, 5.000f, 0.720f, 0.500f, 0.900f},
      {3.200f, 25.000f, 0.050f, 0.800f, 8.000f, 0.820f, 0.300f, 1.000f},
    };
    static const float kBase[NUM_MODES] = {0.5f, 0.75f, 0.25f, 0.5f};
    const float pitch = kBase[mode] * (0.8f + p.pitch * 0.4f);
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (0.8f + p.size * 0.25f); c.decay = v[1] * (0.5f + p.size * 0.8f);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, 0.5f, 0.6f + p.size * 0.4f, pitch, 0.6f, 0.5f, false, gl, gr);
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
