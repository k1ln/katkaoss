#pragma once
// WahDelVerb — auto-wah -> delay -> reverb.
// X=WAH Y=DELAY DEPTH=mix MODE=SLOW/FUNK/DUB/SPACE
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { WAH = 0U, DELAY, DEPTH, MODE, NUM_PARAMS };
  enum { M_SLOW = 0, M_FUNK, M_DUB, M_SPACE, NUM_MODES };
  struct Params { float wah = 0.5f, delay = 0.4f, depth = 0.f; uint32_t mode = M_FUNK; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) { case WAH: params_.wah = param_10bit_to_f32(v); break;
      case DELAY: params_.delay = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break; case 4: mDrive_ = param_10bit_to_f32(v); break; case MODE: params_.mode = v; break; }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SLOW", "FUNK", "DUB", "SPACE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v]; return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize()); dl_.init(alloc_.alloc(48000), 48000); verb_.init(alloc_);
    svf_.reset(); params_ = Params();
  }
  void teardown() override final {}
  void reset() override final { svf_.reset(); }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    float rate = 2.f, dtime = 12000.f, verbAmt = 0.3f;
    switch (p.mode) { case M_SLOW: rate = 0.8f; break; case M_FUNK: rate = 3.5f; break;
      case M_DUB: rate = 1.5f; dtime = 20000.f; verbAmt = 0.5f; break;
      case M_SPACE: rate = 1.f; dtime = 28000.f; verbAmt = 0.8f; break; }
    const float inc = rate / dsp::kSampleRate;
    const float fb = p.delay * 0.85f;
    const float mix = (p.depth + 1.f) * 0.5f;
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dry = (in[0] + in[1]) * 0.5f;
      float mod = lfo_.next(inc) * 0.5f + 0.5f;
      float cutoff = 0.03f + (0.2f + p.wah * 0.6f) * mod;  // sweeping bandpass
      float lp, bp; svf_.process(dry, cutoff, 0.7f, lp, bp);
      float wah = bp * 2.f;
      float d = dl_.process(wah, dtime, fb, 0.25f);
      float rl, rr; verb_.process(d, d, rl, rr, 0.75f, 0.35f);
      float wl = wah + d + rl * verbAmt;
      float wr = wah + d + rr * verbAmt;
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix); out[1] = dsp::driveMix(in[1], mDrive_, wr, mix);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_; dsp::FBDelay dl_; dsp::Reverb verb_; dsp::SVF svf_; dsp::LFO lfo_; Params params_; float mDrive_ = 0.f;
};
