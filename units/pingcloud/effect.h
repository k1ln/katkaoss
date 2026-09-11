#pragma once
// PingCloud — ping-pong delay + grain cloud.
// X=TIME Y=FEEDBACK DEPTH=mix MODE=WIDE/DUB/GRAIN/INFIN
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_WIDE = 0, M_DUB, M_GRAIN, M_INFIN, NUM_MODES };
  struct Params { float time = 0.5f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_WIDE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case TIME: params_.time = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WIDE", "DUB", "GRAIN", "INFIN"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); dL_.init(alloc_.alloc(48000), 48000); dR_.init(alloc_.alloc(48000), 48000);
    cloud_.init(alloc_.alloc(96000), 96000); params_ = Params(); pL_ = pR_ = 0.f;
  }
  void teardown() override final {}
  void reset() override final { pL_ = pR_ = 0.f; }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float grainAmt = 0.f, fbCap = 0.85f;
    switch (p.mode) { case M_WIDE: break; case M_DUB: fbCap = 0.9f; break;
      case M_GRAIN: grainAmt = 1.f; break; case M_INFIN: fbCap = 0.99f; break; }
    const float t = 6000.f + p.time * 40000.f;
    const float fb = p.feedback * fbCap;
    const float damp = 0.25f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      // cross-coupled ping-pong: each tap feeds the opposite side
      float yL = dL_.tap(t);
      float yR = dR_.tap(t);
      dL_.process(dry + yR * fb, t, 0.f, damp);
      dR_.process(yL * fb, t, 0.f, damp);
      float wl = yL, wr = yR;
      if (grainAmt > 0.f) {
        float gl, gr; cloud_.process((yL + yR) * 0.5f, 0.6f, 0.4f, 1.f, 0.7f, 0.5f, false, gl, gr);
        wl += gl; wr += gr;
      }
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix); out[1] = dsp::driveMix(in[1], mDrive_, wr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::FBDelay dL_, dR_; dsp::GrainCloud cloud_; Params params_; float mDrive_ = 0.f; float pL_ = 0.f, pR_ = 0.f;
};
