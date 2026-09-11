#pragma once
// Swarm — dense detuned grain swarm.
// X=SPREAD Y=SIZE DEPTH=mix MODE=BEES/DRONE/STORM/CHOIR
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SPREAD = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_BEES = 0, M_DRONE, M_STORM, M_CHOIR, NUM_MODES };
  struct Params { float spread = 0.7f, size = 0.4f, depth = 0.f; uint32_t mode = M_BEES; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SPREAD: params_.spread = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"BEES", "DRONE", "STORM", "CHOIR"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float pitch = 1.f, gsize = p.size;
    switch (p.mode) { case M_BEES: pitch = 1.f; gsize = p.size * 0.4f; break;
      case M_DRONE: pitch = 1.f; gsize = dsp::clampf(0.6f + p.size * 0.4f, 0.f, 1.f); break;
      case M_STORM: pitch = 0.5f + p.spread; gsize = p.size * 0.5f; break;
      case M_CHOIR: pitch = 1.5f; gsize = dsp::clampf(0.5f + p.size * 0.5f, 0.f, 1.f); break; }
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 1.f, gsize, pitch, p.spread, 0.5f, false, gl, gr);
      out[0] = dsp::lerp(in[0], gl, mix); out[1] = dsp::lerp(in[1], gr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; Params params_;
};
