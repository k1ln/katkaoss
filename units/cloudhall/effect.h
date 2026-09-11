#pragma once
// CloudHall — grain cloud poured into a huge hall.
// X=TEXTURE Y=SIZE DEPTH=mix MODE=HALL/CHURCH/CAVE/VOID
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TEXTURE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_HALL = 0, M_CHURCH, M_CAVE, M_VOID, NUM_MODES };
  struct Params { float texture = 0.5f, size = 0.6f, depth = 0.f; uint32_t mode = M_HALL; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case TEXTURE: params_.texture = param_10bit_to_f32(v); break;
      case SIZE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"HALL", "CHURCH", "CAVE", "VOID"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float room = 0.75f, damp = 0.4f, extra = 0.f;
    switch (p.mode) { case M_HALL: room = 0.75f; break; case M_CHURCH: room = 0.85f; damp = 0.25f; break;
      case M_CAVE: room = 0.8f; damp = 0.6f; break; case M_VOID: room = 0.92f; damp = 0.2f; extra = 0.05f; break; }
    room = dsp::clampf(room + p.size * 0.08f, 0.f, 1.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f; float gl, gr;
      cloud_.process(dry, 0.6f, p.size, 1.f, 0.4f + p.texture * 0.5f, p.texture, false, gl, gr);
      float rl, rr; verb_.process(gl + dry * 0.3f, gr + dry * 0.3f, rl, rr, room, damp, extra);
      out[0] = dsp::driveMix(in[0], mDrive_, gl * 0.5f + rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr * 0.5f + rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
