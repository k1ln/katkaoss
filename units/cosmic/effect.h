#pragma once
// Cosmic — pitch shift -> delay -> reverb wash.
// X=PITCH Y=TIME DEPTH=mix MODE=RISE/FALL/WARP/BLKHOLE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { PITCH = 0U, TIME, DEPTH, MODE, NUM_PARAMS };
  enum { M_RISE = 0, M_FALL, M_WARP, M_BLKHOLE, NUM_MODES };
  struct Params { float pitch = 0.5f, time = 0.5f, depth = 0.f; uint32_t mode = M_RISE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"RISE", "FALL", "WARP", "BLKHOLE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); ps_.init(alloc_.alloc(24000), 24000);
    dl_.init(alloc_.alloc(72000), 72000); verb_.init(alloc_); params_ = Params(); fb_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { fb_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 2.f, extra = 0.f, dfb = 0.6f;
    switch (p.mode) { case M_RISE: base = 2.f; break; case M_FALL: base = 0.5f; break;
      case M_WARP: base = 1.5f; dfb = 0.75f; break; case M_BLKHOLE: base = 0.5f; extra = 0.06f; dfb = 0.85f; break; }
    float ratio = base * (0.9f + p.pitch * 0.2f);
    const float dtime = 6000.f + p.time * 60000.f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float shifted = ps_.process(dry + fb_ * 0.5f, ratio);
      float d = dl_.process(shifted, dtime, dfb, 0.25f);
      float rl, rr; verb_.process(d, d, rl, rr, 0.85f, 0.3f, extra);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, d + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, d + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::PitchShifter ps_; dsp::FBDelay dl_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f; float fb_ = 0.f;
};
