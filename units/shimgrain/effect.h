#pragma once
// ShimGrain — a sustained, pitched grain cloud feeding a shimmer loop: the
// grains are already transposed, then the tail keeps climbing.
// X=GRAIN (size)  Y=SHIMMER (loop amount + length)  DEPTH=mix  MODE=OCT/5TH/2OCT/DUST  DRIVE=grit
//   OCT / 5TH / 2OCT  grains and loop at that interval
//   DUST  sparse percussive octave sparkles, wide, into the loop
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
  enum { GRAIN = 0U, SHIMMER, DEPTH, MODE, NUM_PARAMS };
  enum { M_OCT = 0, M_5TH, M_2OCT, M_DUST, NUM_MODES };
  struct Params { float grain = 0.5f, shimmer = 0.5f, depth = 0.f; uint32_t mode = M_OCT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case SHIMMER: params_.shimmer = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"OCT", "5TH", "2OCT", "DUST"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    verb_.init(alloc_, 2.4f, 0.03f);
    ps_.init(alloc_.alloc(7208), 7208);
    knobs_.reset(); fb_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_OCT;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.646f, 0.610f, 0.661f, 1.096f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.85f, 0.f, 0.f, 0.f, 0.3f, 0.3f, 0.7f},  // OCT
      {0.85f, 0.f, 0.f, 0.f, 0.3f, 0.3f, 0.7f},  // 5TH
      {0.85f, 0.f, 0.f, 0.f, 0.3f, 0.3f, 0.7f},  // 2OCT
      {0.05f, 0.f, 0.f, 0.f, 0.4f, 1.0f, 1.0f},  // DUST
    };
    static const float kRatio[NUM_MODES] = {2.f, 1.5f, 4.f, 2.f}, kDens[NUM_MODES] = {0.7f, 0.7f, 0.7f, 0.25f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float ratio = kRatio[mode];
    dsp::Reverb::Config c;
    c.size = 2.0f; c.decay = 4.f + p.shimmer * 7.f; c.preDelay = 0.02f;
    c.damp = 0.25f; c.diffusion = 0.8f; c.modDepth = 8.f; c.lowCut = 0.15f;
    verb_.setConfig(c);
    const float shim = p.shimmer * 0.7f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, kDens[mode], p.grain, ratio, 0.5f, 0.5f, false, gl, gr);
      const float g = (gl + gr) * 0.5f;
      const float feed = g + dsp::softLimit(ps_.process(fb_, ratio) * shim);
      float rl, rr; verb_.process(feed + gl * 0.3f, feed + gr * 0.3f, rl, rr);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, rl + gl * 0.3f, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, rr + gr * 0.3f, mix, lvl);
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
