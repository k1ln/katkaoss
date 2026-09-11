#pragma once
// Glitch — beat repeat -> bitcrush -> feedback delay.
// X=RATE  Y=CRUSH  DEPTH=mix  MODE=STUT/REPEAT/TAPE/MANGLE  DRIVE=grit
//
// X picks the slice length on a power-of-two grid around the mode's base
// division (so it stays on the beat), Y the bit depth.
//   STUT   16th slices x4, gated, dry-ish
//   REPEAT 8th slices x2, legato, dub echo
//   TAPE   8th slices that slow down every repeat (tape stop), warm echo
//   MANGLE 32nd slices played backwards and faster, heavy crush and feedback
//
// (Previously this ran a grain cloud with freeze hard-wired on, so its buffer
// never recorded anything and every mode was silent.)
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { RATE = 0U, CRUSH, DEPTH, MODE, NUM_PARAMS };
  enum { M_STUT = 0, M_REPEAT, M_TAPE, M_MANGLE, NUM_MODES };
  struct Params { float rate = 0.5f, crush = 0.4f, depth = 0.f; uint32_t mode = M_STUT; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case RATE: params_.rate = param_10bit_to_f32(v); break;
      case CRUSH: params_.crush = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"STUT", "REPEAT", "TAPE", "MANGLE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    rep_.init(alloc_.alloc(192000), 192000);
    dl_.init(alloc_.alloc(48000), 48000);
    params_ = Params();
    t_ = 0; slice_ = 0; recorded_ = 0; beatSamples_ = 24000.f; punch_ = false;
    rate_ = 1.f; resync_ = false;
  }
  void teardown() override final {}
  void reset() override final {}

  void setTempo(float bpm) override final {
    if (bpm >= 20.f && bpm <= 400.f) beatSamples_ = 60.f / bpm * dsp::kSampleRate;
  }
  void tempo4ppqnTick(uint32_t counter) override final {
    if ((counter & 3U) == 0U) resync_ = true;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    //                  division repeats gate  rate   rateStep  dlyBeats  fb    trim
    static const float kM[NUM_MODES][8] = {
      {0.250f, 4.f, 0.55f,  1.0f,  0.00f, 0.75f, 0.30f, 0.90f},  // STUT
      {0.500f, 2.f, 1.00f,  1.0f,  0.00f, 0.75f, 0.60f, 0.75f},  // REPEAT
      {0.500f, 4.f, 1.00f,  1.0f, -0.18f, 1.00f, 0.65f, 0.80f},  // TAPE
      {0.125f, 6.f, 0.80f, -1.5f,  0.00f, 0.25f, 0.78f, 0.65f},  // MANGLE
    };
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_STUT;
    const float *m = kM[mode];
    // X: x2, x1, x1/2, x1/4 of the base division — always a power of two, so on grid
    const int sh = (int)(p.rate * 3.99f);
    const uint32_t sliceLen = (uint32_t)(beatSamples_ * m[0] * 2.f / (float)(1 << sh));
    const uint32_t repeats = (uint32_t)m[1];
    const float levels = exp2f(15.f - p.crush * 13.f);
    const float dtime = dsp::clampf(beatSamples_ * m[5], 480.f, 47000.f);
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.202f, 1.000f, 0.871f, 1.000f};  // @auto-level
    const float lvl = kLevel[mode];

    if (resync_) {
      const float beats = (float)t_ / beatSamples_;
      t_ = (uint32_t)((float)(uint32_t)(beats + 0.5f) * beatSamples_);
      resync_ = false;
    }
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      if (punch_) { t_ = 0; punch_ = false; }
      if (sliceLen > 0 && t_ % sliceLen == 0) {
        const uint32_t k = slice_ % repeats;
        if (k == 0) {
          if (recorded_ >= sliceLen) rep_.trigger((float)sliceLen);
          else rep_.release();
        }
        // TAPE: every repeat plays slower than the last
        rate_ = m[3] * (1.f + m[4] * (float)k);
        if (rate_ > 0.f && rate_ < 0.25f) rate_ = 0.25f;
        ++slice_;
      }
      ++t_;
      if (recorded_ < 0x7fffffffU) ++recorded_;

      float g = rep_.process(dry, rate_, m[2]);
      g = roundf(dsp::softclip(g) * levels) / levels;  // crush
      const float d = dl_.process(g, dtime, m[6], 0.2f);
      const float w = (g + d * 0.7f) * m[7];
      out[0] = dsp::driveMix(in[0], mDrive_, w, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, w, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final {
    if (phase == k_unit_touch_phase_began) { punch_ = true; slice_ = 0; }
  }

 private:
  dsp::BufferAllocator alloc_;
  dsp::BeatRepeat rep_;
  dsp::FBDelay dl_;
  Params params_; float mDrive_ = 0.f;
  uint32_t t_ = 0, slice_ = 0, recorded_ = 0;
  float beatSamples_ = 24000.f, rate_ = 1.f;
  volatile bool punch_ = false, resync_ = false;
};
