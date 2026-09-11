#pragma once
// WahDelVerb — auto-wah -> tempo-synced delay -> reverb.
// X=WAH (sweep range)  Y=DELAY (feedback)  DEPTH=mix  MODE=SLOW/FUNK/DUB/SPACE  DRIVE=grit
// The wah sweeps a resonant band-pass over a real wah range (300 Hz up to
// ~4.8 kHz). (It used to sweep up to 83% of the sample rate, where the filter
// sat on the edge of stability and rang at 17 kHz.)
//   SLOW  lazy sweep, 1/4 echoes      FUNK  fast quack, 1/8D echoes, dry-ish
//   DUB   ping-ponged 1/4D echoes into a room   SPACE  slow sweep, 1/2 echoes into a hall
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
    switch (i) {
      case WAH: params_.wah = param_10bit_to_f32(v); break;
      case DELAY: params_.delay = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"SLOW", "FUNK", "DUB", "SPACE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 2.1f, false);
    verb_.init(alloc_, 2.0f, 0.f);
    svfL_.reset(); svfR_.reset(); ph_ = 0.f; beatSamples_ = 24000.f;
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}
  void setTempo(float bpm) override final {
    if (bpm >= 20.f && bpm <= 400.f) beatSamples_ = 60.f / bpm * dsp::kSampleRate;
  }
  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_FUNK;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.201f, 1.445f, 1.289f, 1.148f};  // @auto-level
    const float lvl = kLevel[mode];
    //                   rateHz beats  cross hiCut  rSize rDecay send  res   trim
    static const float kM[NUM_MODES][9] = {
      {0.8f, 1.00f, 0.0f, 0.35f, 1.0f, 2.0f, 0.30f, 0.70f, 1.0f},  // SLOW
      {3.5f, 0.75f, 0.0f, 0.25f, 0.6f, 1.0f, 0.15f, 0.80f, 1.0f},  // FUNK
      {1.5f, 1.50f, 1.0f, 0.60f, 0.8f, 1.5f, 0.35f, 0.70f, 1.0f},  // DUB
      {1.0f, 2.00f, 0.3f, 0.40f, 1.8f, 5.0f, 0.80f, 0.65f, 0.85f}, // SPACE
    };
    const float *m = kM[mode];
    const float inc = m[0] / dsp::kSampleRate;
    const float octs = 2.f + p.wah * 2.f;   // 2..4 octaves of sweep above 300 Hz
    dsp::StereoDelay::Config d;
    d.timeL = d.timeR = beatSamples_ * m[1];
    d.feedback = p.delay * 0.95f;
    d.cross = m[2]; d.hiCut = m[3]; d.loCut = 0.25f; d.glide = 0.002f;
    dly_.setConfig(d);
    dsp::Reverb::Config c;
    c.size = m[4]; c.decay = m[5]; c.damp = 0.35f; c.diffusion = 0.75f; c.modDepth = 4.f;
    verb_.setConfig(c);
    const float send = m[6], res = m[7], trim = m[8];
    const float bpNorm = 2.5f * (1.f - res);
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      ph_ += inc; if (ph_ >= 1.f) ph_ -= 1.f;
      const float sweep = 0.5f + 0.5f * dsp::fastSin01(ph_);
      const float cutoff = 300.f * exp2f(sweep * octs) / dsp::kSampleRate;
      float lp, bpL, bpR;
      svfL_.process(in[0], cutoff, res, lp, bpL);
      svfR_.process(in[1], cutoff, res, lp, bpR);
      // band-pass peak gain is ~1/q = 1/(1 - res); normalise so resonance
      // changes the tone, not the level (it hit 10x full scale on hot input)
      const float wl = bpL * bpNorm, wr = bpR * bpNorm;
      float dl, dr; dly_.process(wl, wr, dl, dr);
      float rl, rr; verb_.process(dl, dr, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, wl + dl + rl * send, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, wr + dr + rr * send, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::StereoDelay dly_;
  dsp::Reverb verb_;
  dsp::SVF svfL_, svfR_;
  float ph_ = 0.f, beatSamples_ = 24000.f;
  Params params_;
  float mDrive_ = 0.f;
};
