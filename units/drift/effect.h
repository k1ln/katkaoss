#pragma once
// Drift — stereo modulation: flanger, phaser, chorus, jet. L and R sweep 90
// degrees apart. The wet signal already contains the dry/delayed sum, so the
// comb notches are there even at full wet (previously full wet was just a
// wobbling delay: vibrato, no flange).
// X=RATE  Y=FEEDBACK  DEPTH=mix  MODE=FLANGER/PHASER/CHORUS/JET  DRIVE=grit
//   FLANGER  1-6 ms sweep, positive feedback: classic metallic swoosh
//   PHASER   4-stage vintage phaser
//   CHORUS   15-25 ms, no feedback, smooth doubling
//   JET      very slow, deep, negative (inverted) feedback: the tape-flange jet
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_FLANGER = 0, M_PHASER, M_CHORUS, M_JET, NUM_MODES };
  struct Params { float rate = 0.3f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_FLANGER; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FLANGER", "PHASER", "CHORUS", "JET"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dl_[0].init(alloc_.alloc(2048), 2048);
    dl_[1].init(alloc_.alloc(2048), 2048);
    for (int c = 0; c < 2; ++c) { fb_[c] = 0.f; for (int i = 0; i < 4; ++i) z_[c][i] = 0.f; }
    ph_ = 0.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_FLANGER;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.109f, 1.000f, 1.380f, 1.288f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   rateLo rateHi baseD  modD   fbSign fbMax
    static const float kM[NUM_MODES][6] = {
      {0.05f, 2.0f,  48.f, 240.f,  1.f, 0.85f},  // FLANGER
      {0.05f, 2.5f,   0.f,   0.f,  1.f, 0.80f},  // PHASER
      {0.10f, 1.5f, 720.f, 480.f,  0.f, 0.00f},  // CHORUS
      {0.02f, 0.3f,  30.f, 330.f, -1.f, 0.92f},  // JET
    };
    const float *m = kM[mode];
    const float inc = (m[0] + p.rate * (m[1] - m[0])) / dsp::kSampleRate;
    const float fb = p.feedback * m[5] * m[4];
    const bool phaser = (mode == M_PHASER);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      float w[2];
      for (int c = 0; c < 2; ++c) {
        float q = ph_ + (c ? 0.25f : 0.f); if (q >= 1.f) q -= 1.f;
        const float s = 0.5f + 0.5f * dsp::fastSin01(q);
        float y;
        if (phaser) {
          const float g = 0.1f + 0.8f * s;
          float x = in[c] + dsp::softLimit(fb_[c] * fb);
          for (int i = 0; i < 4; ++i) { const float o = -g * x + z_[c][i]; z_[c][i] = x + g * o; x = o; }
          y = x;
        } else {
          dl_[c].write(in[c] + dsp::softLimit(fb_[c] * fb));
          y = dl_[c].read(m[2] + s * m[3] + 2.f);
        }
        fb_[c] = y;
        w[c] = (in[c] + y) * 0.5f;   // the notches live in this sum
      }
      out[0] = dsp::driveMix(in[0], mDrive_, w[0], mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, w[1], mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::DelayLine dl_[2];
  float fb_[2] = {0.f, 0.f}, z_[2][4] = {{0.f}}, ph_ = 0.f;
  Params params_;
  float mDrive_ = 0.f;
};
