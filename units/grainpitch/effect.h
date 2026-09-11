#pragma once
// GrainPitch — pitched grain cloud harmonizer.
// X=PITCH Y=SIZE DEPTH=mix MODE=UNISON/OCT+/5TH/OCT-
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { PITCH = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_UNISON = 0, M_OCTUP, M_5TH, M_OCTDN, NUM_MODES };
  struct Params { float pitch = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_OCTUP; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"UNISON", "OCT+", "5TH", "OCT-"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 1.f;
    switch (p.mode) { case M_UNISON: base = 1.f; break; case M_OCTUP: base = 2.f; break;
      case M_5TH: base = 1.5f; break; case M_OCTDN: base = 0.5f; break; }
    float pitch = base * (0.9f + p.pitch * 0.2f);  // PITCH fine-tunes
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 1.f, p.size, pitch, 0.5f, 0.5f, false, gl, gr);
      out[0] = dsp::lerp(in[0], gl, mix); out[1] = dsp::lerp(in[1], gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; Params params_;
};
