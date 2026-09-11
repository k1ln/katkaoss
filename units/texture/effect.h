#pragma once
// Texture — sustained granular textures from whatever you feed it.
// X=POSITION (how far back the grains reach)  Y=SIZE  DEPTH=mix  MODE=SPARSE/SOFT/DENSE/HAZE  DRIVE=grit
//   SPARSE  scattered percussive specks with gaps     SOFT  gentle overlapping cloud
//   DENSE   solid gated wall                          HAZE  wide, blurred, size and pitch smeared
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
  enum { POSITION = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SPARSE = 0, M_SOFT, M_DENSE, M_HAZE, NUM_MODES };
  struct Params { float position = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_SOFT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case POSITION: params_.position = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SPARSE", "SOFT", "DENSE", "HAZE"};
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
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_SOFT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.622f, 1.084f, 0.804f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.150f, 0.f, 0.f, 0.000f, 0.500f, 1.000f, 0.600f},
      {0.500f, 0.f, 0.f, 0.000f, 0.200f, 0.300f, 0.600f},
      {0.850f, 0.100f, 0.f, 0.000f, 0.600f, 0.300f, 1.000f},
      {0.700f, 0.300f, 0.f, 0.200f, 1.000f, 0.600f, 1.000f},
    };
    static const float kDens[NUM_MODES] = {0.15f, 0.5f, 1.f, 0.8f}, kSpread[NUM_MODES] = {0.3f, 0.5f, 0.7f, 1.f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.size, 1.f, kSpread[mode], p.position, false, gl, gr);
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
