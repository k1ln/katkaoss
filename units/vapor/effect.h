#pragma once
// Vapor — vaporwave slow-down + pitch-drop grains + wow reverb.
// X=SLOW Y=SIZE DEPTH=mix MODE=MALL/DREAM/SLOW/PLUSH
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SLOW = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_MALL = 0, M_DREAM, M_SLOW, M_PLUSH, NUM_MODES };
  struct Params { float slow = 0.5f, size = 0.6f, depth = 0.f; uint32_t mode = M_MALL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SLOW: params_.slow = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"MALL", "DREAM", "SLOW", "PLUSH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(72000), 72000);
    dl_.init(alloc_.alloc(4096), 4096); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float pitch = 0.5f;
    switch (p.mode) { case M_MALL: pitch = 0.75f; break; case M_DREAM: pitch = 0.5f; break;
      case M_SLOW: pitch = 0.4f; break; case M_PLUSH: pitch = 0.6f; break; }
    pitch *= (0.85f + p.slow * 0.3f);
    const float mix = (p.depth + 1.f) * 0.5f;
    const float wowInc = 0.6f / dsp::kSampleRate;  // slow wow/flutter
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 0.6f, dsp::clampf(0.5f + p.size * 0.5f, 0.f, 1.f), pitch, 0.5f, 0.5f, false, gl, gr);
      float g = (gl + gr) * 0.5f;
      dl_.write(g);
      float wow = dl_.read(300.f + (lfo_.next(wowInc) * 0.5f + 0.5f) * 200.f);
      float rl, rr; verb_.process(wow, wow, rl, rr, 0.8f + p.size * 0.15f, 0.5f);
      out[0] = dsp::lerp(in[0], gl * 0.4f + rl, mix); out[1] = dsp::lerp(in[1], gr * 0.4f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::DelayLine dl_; dsp::LFO lfo_; dsp::Reverb verb_; Params params_;
};
