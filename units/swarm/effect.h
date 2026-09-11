#pragma once
// Swarm — a dense cloud of detuned grains.
// X=SPREAD (detune + width)  Y=SIZE  DEPTH=mix  MODE=BEES/DRONE/STORM/CHOIR  DRIVE=grit
//   BEES   tiny grains, a few cents apart: buzzing
//   DRONE  long smooth grains, slow beating
//   STORM  up to an octave of random pitch, scattered, some backwards
//   CHOIR  long gated grains on octaves and fifths: a pitched chorus
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
  enum { SPREAD = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_BEES = 0, M_DRONE, M_STORM, M_CHOIR, NUM_MODES };
  struct Params { float spread = 0.7f, size = 0.4f, depth = 0.f; uint32_t mode = M_BEES; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case SPREAD: params_.spread = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"BEES", "DRONE", "STORM", "CHOIR"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_BEES;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 0.750f, 1.148f, 0.813f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.50f, 0.f, 0.f, 0.0f, 0.3f, 0.3f, 1.f},  // BEES
      {0.85f, 0.f, 0.f, 0.0f, 0.3f, 0.3f, 1.f},  // DRONE
      {0.35f, 0.f, 0.f, 0.3f, 0.8f, 1.0f, 1.f},  // STORM
      {0.80f, 0.f, 3.f, 0.0f, 0.3f, 0.4f, 1.f},  // CHOIR
    };
    //                     jitBase jitX   sizeBase sizeY
    static const float kJ[NUM_MODES][4] = {
      {0.20f,  1.0f, 0.00f, 0.35f},
      {0.08f,  0.3f, 0.70f, 0.30f},
      {1.00f, 11.0f, 0.00f, 0.50f},
      {5.00f,  7.0f, 0.55f, 0.45f},
    };
    dsp::GrainCloud::Texture t = dsp::texture(kT[mode]);
    t.pitchJitter = kJ[mode][0] + p.spread * kJ[mode][1];
    t.width = 0.3f + p.spread * 0.7f;
    cloud_.setTexture(knobs_.apply(t));
    const float gsize = kJ[mode][2] + p.size * kJ[mode][3];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, 1.f, gsize, 1.f, p.spread, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::GrainKnobs knobs_;
  Params params_;
  float mDrive_ = 0.f;
};
