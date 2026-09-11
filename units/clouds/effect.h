#pragma once
/*
 *  File: effect.h — Clouds
 *
 *  Granular texture / cloud generator with a reverb wash.
 *  X = TEXTURE (grain position + stereo spread)
 *  Y = SIZE    (grain size + reverb amount)
 *  DEPTH = dry/wet
 *  MODE  = GRAIN / CLOUD / DENSE / FREEZE
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }  // 1 MB

  enum { TEXTURE = 0U, SIZE, DEPTH, MODE, NUM_PARAMS };
  enum { M_GRAIN = 0, M_CLOUD, M_DENSE, M_FREEZE, NUM_MODES };

  struct Params {
    float texture = 0.5f;
    float size = 0.5f;
    float depth = 0.f;
    uint32_t mode = M_CLOUD;
  };

  inline void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case TEXTURE: params_.texture = param_10bit_to_f32(value); break;
      case SIZE: params_.size = param_10bit_to_f32(value); break;
      case DEPTH: params_.depth = value / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(value); break;
      case MODE: params_.mode = value; break;
      default: break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *modes[NUM_MODES] = {"GRAIN", "CLOUD", "DENSE", "FREEZE"};
    if (index == MODE && value >= 0 && value < NUM_MODES) return modes[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final {
    alloc_.init(allocated_buffer, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);  // 2s record buffer
    verb_.init(alloc_);
    params_ = Params();
  }

  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float density = 0.5f, spread = 0.5f;
    bool freeze = false;
    switch (p.mode) {
      case M_GRAIN: density = 0.35f; spread = 0.3f; break;
      case M_CLOUD: density = 0.7f; spread = 0.6f; break;
      case M_DENSE: density = 1.f; spread = 0.85f; break;
      case M_FREEZE: density = 0.85f; spread = 0.7f; freeze = true; break;
      default: break;
    }
    spread = dsp::clampf(spread * (0.4f + p.texture), 0.f, 1.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    const float verbAmt = p.size;

    for (const float *end = out + frames * 2; out != end; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr;
      cloud_.process(dry, density, p.size, 1.f, spread, p.texture, freeze, gl, gr);

      float rl, rr;
      verb_.process(gl, gr, rl, rr, 0.85f, 0.35f);   // bigger, longer hall
      float wl = gl + rl * (0.8f + verbAmt);          // stronger reverb send
      float wr = gr + rr * (0.8f + verbAmt);

      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix);
      out[1] = dsp::driveMix(in[1], mDrive_, wr, mix);
    }
  }

  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f;
};
