#pragma once
// Meltdown — wavefolder into a drifting space.
// X=FOLD  Y=SIZE  DEPTH=mix  MODE=WARM/HARSH/LIQUID/OOZE  DRIVE=grit
// The fold is level-compensated (more fold = more harmonics, not more volume).
//   WARM    soft fold into a small warm room    HARSH  hard fold, bright tight room
//   LIQUID  folded then pitch-drifted into a modulated hall
//   OOZE    heavy slow drift into a huge dark space
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { FOLD = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_WARM = 0, M_HARSH, M_LIQUID, M_OOZE, NUM_MODES };
  struct Params { float fold = 0.4f, size = 0.6f, depth = 0.f; uint32_t mode = M_WARM; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case FOLD: params_.fold = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WARM", "HARSH", "LIQUID", "OOZE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dl_.init(alloc_.alloc(8192), 8192);
    verb_.init(alloc_, 2.6f, 0.f);
    ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_WARM;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {0.912f, 0.785f, 0.668f, 0.684f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   foldMax drift  rate   size  decay damp  diff  foldLvl send
    static const float kM[NUM_MODES][9] = {
      {3.f,  20.f, 0.40f, 0.6f, 1.0f, 0.60f, 0.70f, 0.6f, 0.6f},  // WARM
      {8.f,   0.f, 0.40f, 0.5f, 0.8f, 0.05f, 0.45f, 0.8f, 0.4f},  // HARSH
      {5.f, 200.f, 0.40f, 1.5f, 3.5f, 0.30f, 0.80f, 0.4f, 0.9f},  // LIQUID
      {5.f, 420.f, 0.15f, 2.4f, 8.0f, 0.75f, 0.80f, 0.3f, 1.0f},  // OOZE
    };
    const float *m = kM[mode];
    const float drive = 1.f + p.fold * m[0];
    const float norm = 1.f / (1.f + (drive - 1.f) * 0.45f);  // fold adds harmonics, not level
    dsp::Reverb::Config c;
    c.size = m[3] * (0.6f + p.size * 0.8f);
    c.decay = m[4] * (0.5f + p.size);
    c.damp = m[5]; c.diffusion = m[6]; c.modDepth = 4.f;
    verb_.setConfig(c);
    const float inc = m[2] / dsp::kSampleRate, drift = m[1], foldLvl = m[7], send = m[8];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      const float folded = dsp::wavefold(dry, drive) * norm;
      dl_.write(folded);
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      const float drifted = drift > 0.f ? dl_.read(200.f + (0.5f + 0.5f * dsp::fastSin01(ph_)) * drift) : folded;
      float rl, rr; verb_.process(drifted, drifted, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, folded * foldLvl + rl * send, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, folded * foldLvl + rr * send, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine dl_;
  dsp::Reverb verb_;
  float ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
