#pragma once
// Grain — dry granular delay: the grains themselves, no space.
// X=SCATTER (density + position spray)  Y=SIZE  DEPTH=mix  MODE=FWD/REV/PITCH/WILD  DRIVE=grit
//   FWD    steady forward grains        REV   every grain backwards
//   PITCH  Y picks the interval instead of the size: -12..+12 semitones, snapped
//   WILD   random pitch (+/-1 oct), direction, size and timing
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
  enum { M_FWD = 0, M_REV, M_PITCH, M_WILD, NUM_MODES };
  struct Params { float scatter = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_FWD; };

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
    static const char *m[NUM_MODES] = {"FWD", "REV", "PITCH", "WILD"};
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
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_FWD;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.084f, 1.513f, 1.000f, 1.274f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.500f, 0.f, 0.f, 0.000f, 0.000f, 0.000f, -1.000f},
      {0.150f, 0.f, 0.f, 1.000f, 0.100f, 0.100f, 0.300f},
      {0.650f, 0.f, 0.f, 0.000f, 0.000f, 0.000f, -1.000f},
      {0.350f, 12.f, 0.f, 0.300f, 0.800f, 0.800f, 1.000f},
    };
    float pitch = 1.f, gsize = p.size;
    if (mode == M_PITCH) { pitch = exp2f(floorf(p.size * 24.f - 12.f + 0.5f) * (1.f / 12.f)); gsize = 0.55f; }
    const float density = 0.4f + p.scatter * 0.6f;
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, density, gsize, pitch, p.scatter, 0.5f, false, gl, gr);
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
