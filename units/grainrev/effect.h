#pragma once
// GrainRev — a cloud of backwards grains.
// X=SCATTER  Y=SIZE  DEPTH=mix  MODE=SLOW/MED/FAST/CHAOS  DRIVE=grit
//   SLOW  half-speed, long soft reverse swells     MED  real-speed reverse
//   FAST  1.5x speed, short percussive reverse ticks
//   CHAOS mostly backwards, random pitch and timing
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
  enum { SCATTER = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SLOW = 0, M_MED, M_FAST, M_CHAOS, NUM_MODES };
  struct Params { float scatter = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_MED; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case SCATTER: params_.scatter = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SLOW", "MED", "FAST", "CHAOS"};
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
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_MED;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.841f, 1.000f, 1.396f, 1.189f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.850f, 0.f, 0.f, 1.000f, 0.100f, 0.100f, -1.000f},
      {0.600f, 0.f, 0.f, 1.000f, 0.500f, 0.400f, 1.000f},
      {0.150f, 0.f, 0.f, 1.000f, 0.300f, 0.300f, -1.000f},
      {0.400f, 12.f, 0.f, 0.700f, 0.800f, 0.900f, 1.000f},
    };
    static const float kPitch[NUM_MODES] = {0.5f, 1.f, 1.5f, 1.f}, kDens[NUM_MODES] = {0.3f, 0.5f, 0.9f, 1.f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.size, kPitch[mode], p.scatter, 0.5f, false, gl, gr);
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
