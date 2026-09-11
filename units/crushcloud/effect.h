#pragma once
// CrushCloud — bitcrusher -> grain cloud -> small space.
// X=CRUSH (bit depth)  Y=GRAIN (size)  DEPTH=mix  MODE=CLEAN/GRIT/CRUSH/NUKE  DRIVE=grit
// Each mode drives harder into the crusher; the crushed level is compensated.
//   CLEAN  crushed but undriven, soft grains    GRIT  driven, shorter grains
//   CRUSH  hard, semitone-jumping grains         NUKE  fully blown, random pitch, reversed, bright
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
  enum { CRUSH = 0U, GRAIN, DEPTH, MODE, NUM_PARAMS };
  enum { M_CLEAN = 0, M_GRIT, M_CRUSH, M_NUKE, NUM_MODES };
  struct Params { float crush = 0.5f, grain = 0.5f, depth = 0.f; uint32_t mode = M_GRIT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case CRUSH: params_.crush = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"CLEAN", "GRIT", "CRUSH", "NUKE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 1.6f, 0.0f);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_GRIT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 0.902f, 1.000f, 1.274f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.500f, 0.f, 0.f, 0.000f, 0.200f, 0.300f, 0.600f},
      {0.300f, 0.f, 0.f, 0.000f, 0.200f, 0.300f, 0.700f},
      {0.150f, 5.f, 1.f, 0.100f, 0.300f, 0.500f, 0.800f},
      {0.050f, 12.f, 0.f, 0.300f, 0.600f, 1.000f, 1.000f},
    };
    static const float kV[NUM_MODES][8] = {
      {1.000f, 1.500f, 0.000f, 0.400f, 3.000f, 0.750f, 1.000f, 0.500f},
      {0.800f, 1.200f, 0.000f, 0.500f, 2.000f, 0.700f, 1.000f, 0.500f},
      {0.600f, 1.000f, 0.000f, 0.200f, 2.000f, 0.600f, 1.000f, 0.500f},
      {1.400f, 3.000f, 0.000f, 0.000f, 5.000f, 0.750f, 1.000f, 0.500f},
    };
    static const float kGain[NUM_MODES] = {1.f, 2.f, 4.f, 8.f};
    const float drive = kGain[mode], comp = 1.f / (1.f + (drive - 1.f) * 0.35f);
    const float levels = exp2f(15.f - p.crush * 13.f);
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float *v = kV[mode];
    dsp::Reverb::Config c;
    c.size = v[0] * (1.f); c.decay = v[1] * (1.f);
    c.preDelay = v[2]; c.damp = v[3]; c.modDepth = v[4]; c.diffusion = v[5];
    c.modRate = 0.45f; c.lowCut = 0.12f;
    verb_.setConfig(c);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      const float crushed = roundf(dsp::softclip(dry * drive) * levels) / levels * comp;
      float gl, gr; cloud_.process(crushed, 0.7f, p.grain, 1.f, 0.6f, 0.5f, false, gl, gr);
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
