#pragma once
// VoxWah — formant/vowel filter -> grain cloud.
// X=VOWEL Y=GRAIN DEPTH=mix MODE=AEIOU/TALK/CRY/ROBOT
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
    switch (i) { case VOWEL: params_.vowel = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"AEIOU", "TALK", "CRY", "ROBOT"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000);
    f1_.reset(); f2_.reset(); osc_.reset(); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { f1_.reset(); f2_.reset(); }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    // vowel sweep positions two formants (approx a-e-i-o-u across VOWEL)
    float pos = p.vowel;
    if (p.mode == M_TALK) pos = lfo_.next(2.f / dsp::kSampleRate) * 0.5f + 0.5f;
    if (p.mode == M_CRY) pos = lfo_.next(0.8f / dsp::kSampleRate) * 0.5f + 0.5f;
    // formant center frequencies as fraction of Nyquist
    float f1 = 0.012f + pos * 0.02f;         // ~ 300-800 Hz
    float f2 = 0.05f + (1.f - pos) * 0.09f;  // ~ 1200-3300 Hz
    float pitch = (p.mode == M_ROBOT) ? 1.f : 1.f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      if (p.mode == M_ROBOT) dry = dry * (osc_.sine(80.f) * 0.5f + 0.5f);  // robotic carrier
      float lp1, bp1, lp2, bp2;
      f1_.process(dry, f1, 0.85f, lp1, bp1);
      f2_.process(dry, f2, 0.85f, lp2, bp2);
      float vox = bp1 * 1.5f + bp2;
      float gl, gr; cloud_.process(vox, 0.7f, p.grain, pitch, 0.6f, 0.5f, false, gl, gr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::SVF f1_, f2_; dsp::LFO lfo_; dsp::Osc osc_; Params params_; float mDrive_ = 0.f;
};
