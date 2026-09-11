#pragma once
// Glacier — slow pitched-down grains + immense reverb.
// X=PITCH Y=SIZE DEPTH=mix MODE=CALM/FLOW/CALVE/DEEP
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { PITCH = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_CALM = 0, M_FLOW, M_CALVE, M_DEEP, NUM_MODES };
  struct Params { float pitch = 0.5f, size = 0.7f, depth = 0.f; uint32_t mode = M_CALM; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"CALM", "FLOW", "CALVE", "DEEP"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 0.5f; float extra = 0.f;
    switch (p.mode) { case M_CALM: base = 0.5f; break; case M_FLOW: base = 0.75f; break;
      case M_CALVE: base = 0.25f; break; case M_DEEP: base = 0.5f; extra = 0.05f; break; }
    float pitch = base * (0.8f + p.pitch * 0.4f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 0.5f, dsp::clampf(0.6f + p.size * 0.4f, 0.f, 1.f), pitch, 0.6f, 0.5f, false, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, dsp::clampf(0.85f + p.size * 0.13f, 0.f, 1.f), 0.55f, extra);
      out[0] = dsp::lerp(in[0], gl * 0.35f + rl, mix); out[1] = dsp::lerp(in[1], gr * 0.35f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_;
};
