#pragma once
// Glitch — stutter + bitcrush + delay chaos.
// X=RATE Y=CRUSH DEPTH=mix MODE=STUT/REPEAT/TAPE/MANGLE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, CRUSH, DEPTH, MODE, NUM_PARAMS };
  enum { M_STUT = 0, M_REPEAT, M_TAPE, M_MANGLE, NUM_MODES };
  struct Params { float rate = 0.5f, crush = 0.4f, depth = 0.f; uint32_t mode = M_STUT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case RATE: params_.rate = param_10bit_to_f32(v); break;
      case CRUSH: params_.crush = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"STUT", "REPEAT", "TAPE", "MANGLE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000);
    dl_.init(alloc_.alloc(48000), 48000); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float gsize = 0.2f, pitch = 1.f, dtime = 8000.f, fb = 0.4f;
    switch (p.mode) { case M_STUT: gsize = 0.15f; break; case M_REPEAT: gsize = 0.3f; dtime = 12000.f; fb = 0.6f; break;
      case M_TAPE: gsize = 0.25f; pitch = 0.75f; dtime = 16000.f; fb = 0.7f; break;
      case M_MANGLE: gsize = 0.1f; pitch = 1.5f; fb = 0.8f; break; }
    gsize *= (0.5f + p.rate);
    const float levels = exp2f(15.f - p.crush * 13.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr; cloud_.process(dry, 1.f, gsize, pitch, 0.3f, 0.5f, true, gl, gr);  // frozen stutter
      float g = (gl + gr) * 0.5f;
      g = roundf(dsp::softclip(g) * levels) / levels;  // crush
      float d = dl_.process(g, dtime, fb, 0.2f);
      out[0] = dsp::lerp(in[0], g + d, mix); out[1] = dsp::lerp(in[1], g + d, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::FBDelay dl_; Params params_;
};
