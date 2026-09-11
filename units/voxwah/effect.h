#pragma once
// VoxWah — formant (vowel) filter -> grain cloud: the input talks.
// X=VOWEL (a-e-i-o-u)  Y=GRAIN (size)  DEPTH=mix  MODE=AEIOU/TALK/CRY/ROBOT  DRIVE=grit
//   AEIOU  X sets the vowel, smooth grains     TALK   vowels move on their own, choppy
//   CRY    slow wailing sweep, slightly detuned    ROBOT  80 Hz ring-carrier, percussive stepped grains
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
  enum { VOWEL = 0U, GRAIN, DEPTH, MODE, NUM_PARAMS };
  enum { M_AEIOU = 0, M_TALK, M_CRY, M_ROBOT, NUM_MODES };
  struct Params { float vowel = 0.5f, grain = 0.5f, depth = 0.f; uint32_t mode = M_AEIOU; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case VOWEL: params_.vowel = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"AEIOU", "TALK", "CRY", "ROBOT"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    f1_.reset(); f2_.reset(); osc_.reset(); knobs_.reset(); ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_AEIOU;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.603f, 0.692f, 0.513f, 1.380f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.60f, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.6f},  // AEIOU
      {0.35f, 0.f, 0.f, 0.f, 0.4f, 0.7f, 0.7f},  // TALK
      {0.80f, 2.f, 0.f, 0.f, 0.3f, 0.3f, 0.8f},  // CRY
      {0.05f, 5.f, 1.f, 0.f, 0.0f, 0.0f, 0.5f},  // ROBOT
    };
    static const float kRate[NUM_MODES] = {0.f, 2.f, 0.8f, 0.f};
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    const float inc = kRate[mode] / dsp::kSampleRate;
    const bool robot = (mode == M_ROBOT);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float pos = p.vowel;
      if (inc > 0.f) { ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f; pos = 0.5f + 0.5f * dsp::fastSin01(ph_); }
      const float f1 = 0.012f + pos * 0.02f;          // ~ 300-800 Hz
      const float f2 = 0.05f + (1.f - pos) * 0.09f;   // ~ 1.2-3.3 kHz
      if (robot) dry *= osc_.sine(80.f) * 0.5f + 0.5f;
      float lp1, bp1, lp2, bp2;
      f1_.process(dry, f1, 0.85f, lp1, bp1);
      f2_.process(dry, f2, 0.85f, lp2, bp2);
      const float vox = bp1 * 1.5f + bp2;
      float gl, gr; cloud_.process(vox, 0.7f, p.grain, 1.f, 0.6f, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::SVF f1_, f2_;
  dsp::Osc osc_;
  dsp::GrainKnobs knobs_;
  float ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
