#pragma once
// HallVerb — spacious concert halls.
// X=SIZE Y=TONE DEPTH=mix MODE=SMALL/MED/LARGE/EPIC
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SMALL = 0, M_MED, M_LARGE, M_EPIC, NUM_MODES };
  struct Params { float size = 0.6f, tone = 0.5f, depth = 0.f; uint32_t mode = M_MED; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SIZE: params_.size = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SMALL", "MED", "LARGE", "EPIC"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final { alloc_.init(b, getBufferSize()); verb_.init(alloc_); params_ = Params(); }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 0.6f, extra = 0.f;
    switch (p.mode) { case M_SMALL: base = 0.55f; break; case M_MED: base = 0.7f; break;
      case M_LARGE: base = 0.82f; extra = 0.03f; break; case M_EPIC: base = 0.9f; extra = 0.06f; break; }
    const float room = dsp::clampf(base + p.size * 0.25f, 0.f, 1.f);
    const float damp = 1.f - p.tone;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr, room, damp, extra);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
