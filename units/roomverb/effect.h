#pragma once
// RoomVerb — tight room ambiences.
// X=SIZE Y=TONE DEPTH=mix MODE=TIGHT/WOOD/TILE/BOOTH
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { SIZE = 0U, TONE, DEPTH, MODE, NUM_PARAMS };
  enum { M_TIGHT = 0, M_WOOD, M_TILE, M_BOOTH, NUM_MODES };
  struct Params { float size = 0.4f, tone = 0.5f, depth = 0.f; uint32_t mode = M_TIGHT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case SIZE: params_.size = param_10bit_to_f32(v); break;
      case TONE: params_.tone = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"TIGHT", "WOOD", "TILE", "BOOTH"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final { alloc_.init(b, getBufferSize()); verb_.init(alloc_); params_ = Params(); }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float base = 0.35f, tilt = 0.f;
    switch (p.mode) { case M_TIGHT: base = 0.3f; tilt = 0.2f; break;
      case M_WOOD: base = 0.42f; tilt = 0.4f; break;
      case M_TILE: base = 0.5f; tilt = -0.1f; break;
      case M_BOOTH: base = 0.25f; tilt = 0.5f; break; }
    const float room = dsp::clampf(base + p.size * 0.3f, 0.f, 1.f);
    const float damp = dsp::clampf((1.f - p.tone) + tilt, 0.f, 1.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float rl, rr; verb_.process(in[0], in[1], rl, rr, room, damp);
      out[0] = dsp::driveMix(in[0], mDrive_, rl, mix); out[1] = dsp::driveMix(in[1], mDrive_, rr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
