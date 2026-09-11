#pragma once
// Cosmic — pitch shift -> delay -> huge reverb whose tail recirculates through
// the shifter: a pitch cascade dissolving into space (reverb-dominant; for
// discrete rhythmic pitch steps see Riser).
// X=PITCH (fine, +/-10%)  Y=TIME  DEPTH=mix  MODE=RISE/FALL/WARP/BLKHOLE  DRIVE=grit
//   RISE     octave-up cascade into a bright hall
//   FALL     octave-down cascade into a dark hall
//   WARP     fifth-up, wobbling, ping-ponged
//   BLKHOLE  octave-down into a 30 s, near-black void
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { PITCH = 0U, TIME, DEPTH, MODE, NUM_PARAMS };
  enum { M_RISE = 0, M_FALL, M_WARP, M_BLKHOLE, NUM_MODES };
  struct Params { float pitch = 0.5f, time = 0.5f, depth = 0.f; uint32_t mode = M_RISE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case PITCH: params_.pitch = param_10bit_to_f32(v); break;
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"RISE", "FALL", "WARP", "BLKHOLE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    ps_.init(alloc_.alloc(7208), 7208);
    dly_.init(alloc_, 1.4f, false);
    verb_.init(alloc_, 3.0f, 0.06f);
    fb_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_RISE;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.380f, 1.303f, 1.758f, 1.928f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   ratio recirc dFb  cross  wow  rSize rDecay rDamp  pre    trim
    static const float kM[NUM_MODES][10] = {
      {2.0f, 0.45f, 0.45f, 0.0f, 0.f, 2.0f,  7.f, 0.15f, 0.03f, 0.80f},  // RISE
      {0.5f, 0.45f, 0.45f, 0.0f, 0.f, 2.0f,  7.f, 0.55f, 0.03f, 0.85f},  // FALL
      {1.5f, 0.40f, 0.55f, 1.0f, 6.f, 1.7f,  5.f, 0.30f, 0.02f, 0.80f},  // WARP
      {0.5f, 0.55f, 0.60f, 0.3f, 3.f, 2.9f, 30.f, 0.85f, 0.06f, 0.75f},  // BLKHOLE
    };
    const float *m = kM[mode];
    const float ratio = m[0] * (0.9f + p.pitch * 0.2f);
    dsp::StereoDelay::Config d;
    d.timeL = 4800.f + p.time * 57600.f; d.timeR = d.timeL * 0.8f;
    d.feedback = m[2]; d.cross = m[3]; d.wow = m[4]; d.hiCut = 0.3f; d.glide = 0.0008f;
    dly_.setConfig(d);
    dsp::Reverb::Config c;
    c.size = m[5]; c.decay = m[6]; c.damp = m[7]; c.preDelay = m[8];
    c.diffusion = 0.8f; c.modDepth = 10.f;
    verb_.setConfig(c);
    const float recirc = m[1], trim = m[9];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      const float shifted = ps_.process(dry + dsp::softLimit(fb_ * recirc), ratio);
      float dl, dr; dly_.process(shifted, shifted, dl, dr);
      float rl, rr; verb_.process(dl, dr, rl, rr);
      fb_ = (rl + rr) * 0.5f;
      out[0] = dsp::driveMix(in[0], mDrive_, dl * 0.6f + rl, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, dr * 0.6f + rr, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::PitchShifter ps_;
  dsp::StereoDelay dly_;
  dsp::Reverb verb_;
  float fb_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
