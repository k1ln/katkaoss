#pragma once
// RevDelay — reverse delay: every repeat plays backwards.
// X=TIME  Y=FEEDBACK  DEPTH=mix  MODE=REV/SWELL/OCT/SMEAR  DRIVE=grit
//
// X snaps to note values (1/16 .. 1/2) at the NTS-3 tempo; that is also the
// length of each reversed chunk.
//   REV    clean backwards echoes
//   SWELL  backwards echoes blooming into a long hall — the classic pre-verb swell
//   OCT    each backwards repeat an octave higher (in-loop pitch shift)
//   SMEAR  ping-ponged, darkened and wobbling — backwards tape wash
//
// @map x 0 1023 540
// @map y 0 1023 380
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_REV = 0, M_SWELL, M_OCT, M_SMEAR, NUM_MODES };
  struct Params { float time = 0.53f, feedback = 0.37f, depth = 0.f; uint32_t mode = M_REV; };

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
    static const char *m[NUM_MODES] = {"REV", "SWELL", "OCT", "SMEAR"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 2.2f, true);    // reverse reads up to 2x TIME back
    verb_.init(alloc_, 1.2f, 0.f);
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
    //                    cross hiCut loCut  wow  pitch  send  rvSize rvDecay damp  trim
    static const float kM[NUM_MODES][10] = {
      {0.00f, 0.15f, 0.10f, 0.f, 1.0f, 0.00f, 0.6f,  1.0f, 0.4f, 1.00f},  // REV
      {0.20f, 0.30f, 0.15f, 0.f, 1.0f, 1.00f, 1.2f,  6.0f, 0.3f, 0.80f},  // SWELL
      {0.00f, 0.40f, 0.30f, 0.f, 2.0f, 0.25f, 0.9f,  2.5f, 0.2f, 0.95f},  // OCT
      {1.00f, 0.65f, 0.35f, 6.f, 1.0f, 0.35f, 1.0f,  3.0f, 0.6f, 1.00f},  // SMEAR
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_REV];
    const float t = dsp::kNoteBeats[dsp::noteIndex(p.time, 9)] * beatSamples_;

    dsp::StereoDelay::Config c;
    c.timeL = c.timeR = t;
    c.reverse = true;
    c.feedback = p.feedback * 1.1f;
    c.cross = m[0]; c.hiCut = m[1]; c.loCut = m[2]; c.wow = m[3]; c.pitch = m[4];
    c.glide = 0.001f;
    dly_.setConfig(c);
    const float send = m[5], trim = m[9];
    if (send > 0.f) {
      dsp::Reverb::Config r;
      r.size = m[6]; r.decay = m[7]; r.damp = m[8]; r.diffusion = 0.78f; r.modDepth = 8.f;
      verb_.setConfig(r);
    }

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_MODES] = {1.096f, 1.000f, 1.189f, 1.175f};  // @auto-level

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
