#pragma once
// Warp — pitch shifter with feedback (synth-style detune/harmonizer).
// X=PITCH  Y=FEEDBACK  DEPTH=mix  MODE=OCT-/5TH/OCT+/FIFTHUP
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { PITCH = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_OCTDN = 0, M_5TH, M_OCTUP, M_12TH, NUM_MODES };

  struct Params { float pitch = 0.5f, feedback = 0.3f, depth = 0.f; uint32_t mode = M_OCTUP; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"OCT-", "5TH", "OCT+", "12TH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    ps_.init(alloc_.alloc(24000), 24000);
    dl_.init(alloc_.alloc(48000), 48000);
    params_ = Params();
    fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 2.f;
    switch (p.mode) {
      case M_OCTDN: base = 0.5f; break;
      case M_5TH: base = 1.5f; break;
      case M_OCTUP: base = 2.f; break;
      case M_12TH: base = 3.f; break;
    }
    // PITCH fine-tunes +/- around the selected interval
    float ratio = base * (0.9f + p.pitch * 0.2f);
    const float mix = (p.depth + 1.f) * 0.5f;
    const float fbAmt = p.feedback * 0.85f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float shifted = ps_.process(dry + fb_ * fbAmt, ratio);
      fb_ = dl_.process(shifted, 6000.f, 0.3f, 0.2f);
      out[0] = dsp::driveMix(in[0], mDrive_, shifted, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, shifted, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::PitchShifter ps_;
  dsp::FBDelay dl_;
  Params params_; float mDrive_ = 0.f;
  float fb_ = 0.f;
};
