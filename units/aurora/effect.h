#pragma once
// Aurora — grains whose play point slowly drifts through the buffer, into a
// heavily modulated space: a texture that never repeats.
// X=DRIFT (speed)  Y=SIZE  DEPTH=mix  MODE=DAWN/NIGHT/SOLAR/POLAR  DRIVE=grit
//   DAWN   unison, warm light modulation     NIGHT  an octave down, dark
//   SOLAR  octave-up, jumping octaves, bright
//   POLAR  a fifth up, deep slow modulation, wide
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
  enum { DRIFT = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_DAWN = 0, M_NIGHT, M_SOLAR, M_POLAR, NUM_MODES };
  struct Params { float drift = 0.3f, size = 0.6f, depth = 0.f; uint32_t mode = M_DAWN; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case DRIFT: params_.drift = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"DAWN", "NIGHT", "SOLAR", "POLAR"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 2.4f, 0.03f);
    knobs_.reset();
    glide_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_DAWN;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.803f, 0.759f, 0.923f, 0.741f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.800f, 0.f, 0.f, 0.000f, 0.400f, 0.500f, 0.800f},
      {0.850f, 0.f, 0.f, 0.100f, 0.400f, 0.500f, 0.800f},
      {0.600f, 12.f, 2.f, 0.000f, 0.400f, 0.500f, 1.000f},
      {0.850f, 0.f, 0.f, 0.000f, 0.400f, 0.500f, 1.000f},
    };
    static const float kV[NUM_MODES][8] = {
      {2.100f, 8.000f, 0.020f, 0.300f, 14.000f, 0.800f, 0.400f, 1.000f},
      {2.000f, 7.000f, 0.030f, 0.650f, 8.000f, 0.800f, 0.400f, 1.000f},
      {1.600f, 4.000f, 0.020f, 0.100f, 16.000f, 0.800f, 0.400f, 1.000f},
      {2.300f, 9.000f, 0.030f, 0.450f, 24.000f, 0.800f, 0.400f, 1.000f},
    };
    static const float kPitch[NUM_MODES] = {1.f, 0.5f, 2.f, 1.5f};
    const float driftRate = 0.0000008f + p.drift * 0.000006f;
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (1.f); c.decay = v[1] * (0.5f + p.size);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      glide_ += driftRate; if (glide_ >= 1.f) glide_ -= 1.f;
      float gl, gr; cloud_.process(dry, 0.6f, p.size, kPitch[mode], 0.7f, glide_, false, gl, gr);
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
  float glide_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
