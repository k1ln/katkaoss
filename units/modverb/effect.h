#pragma once
// ModVerb — modulated / chorused reverb.
// X=SIZE Y=MODRATE DEPTH=mix MODE=SOFT/LUSH/SEASICK/WOW
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, MODRATE, DEPTH, MODE, NUM_PARAMS };
  enum { M_SOFT = 0, M_LUSH, M_SEASICK, M_WOW, NUM_MODES };
  struct Params { float size = 0.6f, modrate = 0.4f, depth = 0.f; uint32_t mode = M_LUSH; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SIZE: params_.size = param_10bit_to_f32(v); break;
      case MODRATE: params_.modrate = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SOFT", "LUSH", "SEASICK", "WOW"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); dl_.init(alloc_.alloc(4096), 4096); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float depthMod = 40.f, rateScale = 1.f;
    switch (p.mode) { case M_SOFT: depthMod = 25.f; break; case M_LUSH: depthMod = 60.f; break;
      case M_SEASICK: depthMod = 180.f; rateScale = 0.5f; break; case M_WOW: depthMod = 120.f; rateScale = 0.25f; break; }
    const float inc = (0.1f + p.modrate * 4.f) * rateScale / dsp::kSampleRate;
    const float room = 0.6f + p.size * 0.35f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      dl_.write(dry);
      float m = lfo_.next(inc);
      float chorused = dl_.read(600.f + (m * 0.5f + 0.5f) * depthMod);  // modulated pre-delay
      float rl, rr; verb_.process(chorused, chorused, rl, rr, room, 0.3f);
      out[0] = dsp::lerp(in[0], rl, mix); out[1] = dsp::lerp(in[1], rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::DelayLine dl_; dsp::LFO lfo_; dsp::Reverb verb_; Params params_;
};
