#pragma once
// Riser — pitch-shifting feedback delay: every repeat climbs (or falls).
// X=TIME  Y=FEEDBACK  DEPTH=mix  MODE=FIFTH/OCT+/DOWN/SHIMMER  DRIVE=grit
//
// The pitch shifter sits inside the feedback loop, so with high feedback a
// single note becomes an endless staircase. X snaps to note values.
//   FIFTH    each repeat a fifth up
//   OCT+     each repeat an octave up — fast climb into sparkle
//   DOWN     each repeat an octave down — sinks into sub
//   SHIMMER  octave-up repeats ping-ponged into a long hall
//
// @map x 0 1023 380
// @map y 0 1023 700
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_FIFTH = 0, M_OCTUP, M_DOWN, M_SHIMMER, NUM_MODES };
  struct Params { float time = 0.37f, feedback = 0.68f, depth = 0.f; uint32_t mode = M_FIFTH; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"FIFTH", "OCT+", "DOWN", "SHIMMER"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 2.0f, true);
    verb_.init(alloc_, 1.6f, 0.f);
    params_ = Params();
    beatSamples_ = 24000.f;
  }
  void teardown() override final {}
  void reset() override final {}
  void setTempo(float bpm) override final {
    if (bpm >= 20.f && bpm <= 400.f) beatSamples_ = 60.f / bpm * dsp::kSampleRate;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    //                   pitch  cross hiCut loCut  send  rvDecay trim
    static const float kM[NUM_MODES][7] = {
      {1.5f, 0.00f, 0.30f, 0.15f, 0.00f, 1.0f, 1.00f},  // FIFTH
      {2.0f, 0.00f, 0.45f, 0.20f, 0.00f, 1.0f, 1.00f},  // OCT+ — darken so the climb stays musical
      {0.5f, 0.00f, 0.10f, 0.02f, 0.00f, 1.0f, 1.05f},  // DOWN — keep the lows
      {2.0f, 0.80f, 0.40f, 0.25f, 0.90f, 7.0f, 0.80f},  // SHIMMER
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_FIFTH];
    const float t = dsp::kNoteBeats[dsp::noteIndex(p.time)] * beatSamples_;

    dsp::StereoDelay::Config c;
    c.timeL = c.timeR = t;
    c.feedback = p.feedback * 1.05f;
    c.pitch = m[0]; c.cross = m[1]; c.hiCut = m[2]; c.loCut = m[3];
    c.glide = 0.001f;
    dly_.setConfig(c);
    const float send = m[4], trim = m[6];
    if (send > 0.f) {
      dsp::Reverb::Config r;
      r.size = 1.5f; r.decay = m[5]; r.damp = 0.25f; r.diffusion = 0.8f; r.modDepth = 10.f;
      verb_.setConfig(r);
    }

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 0.902f, 1.000f};  // @auto-level

    const float lvl = kLevel[(p.mode < NUM_MODES ? p.mode : 0)];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float dl, dr; dly_.process(in[0], in[1], dl, dr);
      if (send > 0.f) {
        float rl, rr; verb_.process(dl, dr, rl, rr);
        dl += rl * send; dr += rr * send;
      }
      out[0] = dsp::driveMix(in[0], mDrive_, dl, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, dr, mix, trim * lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}
 private:
  dsp::BufferAllocator alloc_;
  dsp::StereoDelay dly_;
  dsp::Reverb verb_;
  Params params_; float mDrive_ = 0.f;
  float beatSamples_ = 24000.f;
};
