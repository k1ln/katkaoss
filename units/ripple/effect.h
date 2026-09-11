#pragma once
// Ripple — lush chorus / ensemble.
// X=RATE  Y=DEPTH  DEPTH=mix  MODE=1V/2V/3V/WIDE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, MOD, DEPTH, MODE, NUM_PARAMS };
  enum { M_1V = 0, M_2V, M_3V, M_WIDE, NUM_MODES };

  struct Params { float rate = 0.3f, mod = 0.5f, depth = 0.f; uint32_t mode = M_2V; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case MOD: params_.mod = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"1V", "2V", "3V", "WIDE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }

  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dl_.init(alloc_.alloc(4096), 4096);
    for (int i = 0; i < 3; ++i) lfo_[i].setPhase(i * 0.33f);
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    int voices = 1;
    switch (p.mode) {
      case M_1V: voices = 1; break;
      case M_2V: voices = 2; break;
      case M_3V: voices = 3; break;
      case M_WIDE: voices = 3; break;
    }
    const float inc = (0.05f + p.rate * 3.f) / dsp::kSampleRate;
    const float modDepth = 300.f + p.mod * 900.f;  // samples
    const float base = 900.f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      dl_.write(dry);
      float wl = 0.f, wr = 0.f;
      for (int vch = 0; vch < voices; ++vch) {
        float m = lfo_[vch].next(inc);
        float d = base + (vch + 1) * 120.f + m * modDepth;
        float s = dl_.read(d);
        if (p.mode == M_WIDE) {
          if (vch & 1) wr += s; else wl += s;
        } else {
          wl += s; wr += s;
        }
      }
      float g = 1.f / voices;
      out[0] = dsp::lerp(in[0], wl * g, mix);
      out[1] = dsp::lerp(in[1], wr * g, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine dl_;
  dsp::LFO lfo_[3];
  Params params_;
};
