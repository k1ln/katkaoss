#pragma once
// CrushCloud — bitcrusher -> grain cloud -> reverb.
// X=CRUSH Y=GRAIN DEPTH=mix MODE=CLEAN/GRIT/CRUSH/NUKE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { CRUSH = 0U, GRAIN, DEPTH, MODE, NUM_PARAMS };
  enum { M_CLEAN = 0, M_GRIT, M_CRUSH, M_NUKE, NUM_MODES };
  struct Params { float crush = 0.5f, grain = 0.5f, depth = 0.f; uint32_t mode = M_GRIT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case CRUSH: params_.crush = param_10bit_to_f32(v); break;
      case GRAIN: params_.grain = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"CLEAN", "GRIT", "CRUSH", "NUKE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); cloud_.init(alloc_.alloc(96000), 96000); verb_.init(alloc_); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    static constexpr float gains[NUM_MODES] = {1.f, 2.f, 4.f, 8.f};
    const float drive = gains[p.mode < NUM_MODES ? p.mode : 0];
    const float levels = exp2f(15.f - p.crush * 13.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float c = dsp::softclip(dry * drive);
      c = roundf(c * levels) / levels;  // bit crush
      float gl, gr; cloud_.process(c, 0.7f, p.grain, 1.f, 0.6f, 0.5f, false, gl, gr);
      float rl, rr; verb_.process(gl, gr, rl, rr, 0.7f, 0.3f);
      out[0] = dsp::driveMix(in[0], mDrive_, gl + rl * 0.5f, mix); out[1] = dsp::driveMix(in[1], mDrive_, gr + rr * 0.5f, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::GrainCloud cloud_; dsp::Reverb verb_; Params params_; float mDrive_ = 0.f;
};
