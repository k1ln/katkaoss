#pragma once
// GranCath — a cathedral fed by choir-like grains: the space dominates, the
// grains land on octaves/fifths so they sing rather than smear.
// X=GRAIN (size)  Y=SIZE (space)  DEPTH=mix  MODE=NAVE/APSE/CRYPT/HEAVEN  DRIVE=grit
//   NAVE    long main hall, octave grains     APSE  brighter, octave + fifth grains
//   CRYPT   dark and low, some reversed grains
//   HEAVEN  octave-shimmer recirculating in the tail
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
  enum { GRAIN = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_NAVE = 0, M_APSE, M_CRYPT, M_HEAVEN, NUM_MODES };
  struct Params { float grain = 0.5f, size = 0.8f, depth = 0.f; uint32_t mode = M_NAVE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"NAVE", "APSE", "CRYPT", "HEAVEN"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 3.3f, 0.07f);
    ps_.init(alloc_.alloc(7208), 7208);
    knobs_.reset(); fb_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_NAVE;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.851f, 0.841f, 1.000f, 0.741f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.80f, 12.f, 2.f, 0.0f, 0.3f, 0.3f, 1.f},  // NAVE
      {0.80f, 12.f, 3.f, 0.0f, 0.3f, 0.3f, 1.f},  // APSE
      {0.70f, 12.f, 2.f, 0.3f, 0.3f, 0.4f, 1.f},  // CRYPT
      {0.85f, 12.f, 3.f, 0.0f, 0.3f, 0.3f, 1.f},  // HEAVEN
    };
    //                    size  decay  pre    damp  shimmer
    static const float kV[NUM_MODES][5] = {
      {2.6f,  8.f, 0.05f, 0.30f, 0.0f},
      {2.3f,  6.f, 0.04f, 0.15f, 0.0f},
      {2.8f, 10.f, 0.06f, 0.70f, 0.0f},
      {3.0f, 12.f, 0.06f, 0.10f, 0.5f},
    };
    const float *v = kV[mode];
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    dsp::Reverb::Config c;
    c.size = v[0] * (0.75f + p.size * 0.3f); c.decay = v[1] * (0.4f + p.size);
    c.preDelay = v[2]; c.damp = v[3]; c.diffusion = 0.82f; c.modDepth = 9.f; c.modRate = 0.35f;
    verb_.setConfig(c);
    const float shimmer = v[4];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, 0.5f, p.grain, 1.f, 0.5f, 0.5f, false, gl, gr);
      float feed = (gl + gr) * 0.5f + dry * 0.25f;
      if (shimmer > 0.f) feed += dsp::softLimit(ps_.process(fb_, 2.f) * shimmer);
      float rl, rr; verb_.process(feed, feed, rl, rr);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl + gl * 0.15f, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr + gr * 0.15f, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::Reverb verb_;
  dsp::PitchShifter ps_;
  dsp::GrainKnobs knobs_;
  float fb_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
