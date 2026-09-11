#pragma once
// Ripple — stereo chorus / ensemble. Every voice has its own LFO phase, and
// voices are spread across the stereo field (previously only WIDE was stereo).
// X=RATE  Y=MOD (depth)  DEPTH=mix  MODE=1V/2V/3V/WIDE  DRIVE=grit
//   1V    one voice, L/R in quadrature: simple stereo chorus
//   2V    two voices: thicker            3V  three voices: ensemble / string machine
//   WIDE  three voices hard-spread plus a slow extra sweep: huge and swirling
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
      case 4: mDrive_ = param_10bit_to_f32(v); break;
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
    for (int i = 0; i < 3; ++i) ph_[i] = (float)i / 3.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_2V;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.148f, 1.122f, 1.000f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];
    static const int kVoices[NUM_MODES] = {1, 2, 3, 3};
    static const float kPan[NUM_MODES] = {1.0f, 0.7f, 0.6f, 1.0f};
    const int voices = kVoices[mode];
    const float inc = (0.05f + p.rate * 3.f) / dsp::kSampleRate;
    const float modDepth = 150.f + p.mod * 700.f;   // samples
    const float pan = kPan[mode];
    const bool wide = (mode == M_WIDE);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      dl_.write(dry);
      float wl = 0.f, wr = 0.f;
      for (int v = 0; v < voices; ++v) {
        ph_[v] += inc * (1.f + 0.13f * (float)v);
        if (ph_[v] >= 1.f) ph_[v] -= 1.f;
        float q = ph_[v] + 0.25f; if (q >= 1.f) q -= 1.f;
        const float base = 700.f + (float)v * 170.f + (wide ? 300.f : 0.f);
        const float sl = dl_.read(base + (0.5f + 0.5f * dsp::fastSin01(ph_[v])) * modDepth);
        const float sr = dl_.read(base + (0.5f + 0.5f * dsp::fastSin01(q)) * modDepth);
        // alternate which side each voice leans to
        const float side = (v & 1) ? -pan : pan;
        wl += sl * (1.f + side) * 0.5f + sr * (1.f - side) * 0.5f;
        wr += sr * (1.f + side) * 0.5f + sl * (1.f - side) * 0.5f;
      }
      const float g = 1.f / sqrtf((float)voices);
      out[0] = dsp::driveMix(in[0], mDrive_, (in[0] + wl * g) * 0.6f, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, (in[1] + wr * g) * 0.6f, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine dl_;
  float ph_[3] = {0.f, 0.f, 0.f};
  Params params_;
  float mDrive_ = 0.f;
};
