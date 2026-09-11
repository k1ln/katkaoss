#pragma once
// Meltdown — wavefolder -> reverb -> slow drift.
// X=FOLD Y=SIZE DEPTH=mix MODE=WARM/HARSH/LIQUID/OOZE
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
    switch (i) { case FOLD: params_.fold = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WARM", "HARSH", "LIQUID", "OOZE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); dl_.init(alloc_.alloc(8192), 8192); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float driveMax = 4.f, modDepth = 60.f, modRate = 0.4f;
    switch (p.mode) { case M_WARM: driveMax = 3.f; break; case M_HARSH: driveMax = 8.f; break;
      case M_LIQUID: driveMax = 5.f; modDepth = 200.f; break;
      case M_OOZE: driveMax = 5.f; modDepth = 400.f; modRate = 0.15f; break; }
    const float drive = 1.f + p.fold * driveMax;
    const float inc = modRate / dsp::kSampleRate;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float folded = dsp::wavefold(dry, drive);
      dl_.write(folded);
      float m = lfo_.next(inc) * 0.5f + 0.5f;
      float drifted = dl_.read(200.f + m * modDepth);  // slow pitch drift
      float rl, rr; verb_.process(drifted, drifted, rl, rr, 0.7f + p.size * 0.25f, 0.35f);
      out[0] = dsp::driveMix(in[0], mDrive_, folded * 0.5f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, folded * 0.5f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::DelayLine dl_; dsp::LFO lfo_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
