#pragma once
// Scatter — grains sprayed across the stereo field.
// X=SPRAY (width + position spray)  Y=SIZE  DEPTH=mix  MODE=NEAR/WIDE/PING/RAIN  DRIVE=grit
//   NEAR  a tight cluster near the centre   WIDE  a smooth wall across the whole field
//   PING  sparse percussive dots hard left and right
//   RAIN  dense tiny droplets at random times, slightly detuned
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
  enum { SPRAY = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_NEAR = 0, M_WIDE, M_PING, M_RAIN, NUM_MODES };
  struct Params { float spray = 0.5f, size = 0.4f, depth = 0.f; uint32_t mode = M_WIDE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case SPRAY: params_.spray = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"NEAR", "WIDE", "PING", "RAIN"};
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
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_WIDE;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.109f, 1.000f, 1.496f, 1.396f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.300f, 0.f, 0.f, 0.000f, 0.200f, 0.300f, 0.300f},
      {0.550f, 0.f, 0.f, 0.000f, 0.300f, 0.400f, 1.000f},
      {0.080f, 0.f, 0.f, 0.000f, 0.000f, 0.000f, 1.000f},
      {0.050f, 3.f, 0.f, 0.200f, 0.600f, 1.000f, 1.000f},
    };
    static const float kDens[NUM_MODES] = {0.6f, 0.6f, 0.35f, 1.f}, kSize[NUM_MODES] = {1.f, 1.f, 0.6f, 0.35f};
    static const float kSpray[NUM_MODES] = {0.4f, 1.f, 1.f, 1.f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.size * kSize[mode], 1.f, p.spray * kSpray[mode], p.spray, false, gl, gr);
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
