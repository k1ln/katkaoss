#pragma once
// TapeDelay — tempo-synced performance delay.
// X=TIME  Y=FEEDBACK  DEPTH=mix  MODE=TAPE/DUB/DIGI/SPACE  DRIVE=grit
//
// X snaps to note values (1/16 .. 1 bar, incl. triplets and dotted) at the
// NTS-3 tempo; moving it glides the tape head, so sweeps bend pitch instead of
// clicking. The top of Y goes past 100% feedback into a controlled runaway.
//   TAPE   wow/flutter, saturated and darkening repeats
//   DUB    ping-pong, band-limited repeats into a short room
//   DIGI   pristine, full-bandwidth, fast glide
//   SPACE  two heads (L = TIME, R = 3/4 TIME) into a hall — rhythmic wash
//
// @map x 0 1023 460
// @map y 0 1023 400
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_TAPE = 0, M_DUB, M_DIGI, M_SPACE, NUM_MODES };
  struct Params { float time = 0.45f, feedback = 0.39f, depth = 0.f; uint32_t mode = M_TAPE; };

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
    static const char *m[NUM_MODES] = {"TAPE", "DUB", "DIGI", "SPACE"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 2.2f, false);
    verb_.init(alloc_, 1.4f, 0.f);
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
    //                   cross hiCut loCut drive  wow  flut  glide    rMul  send  rvSize rvDecay trim
    static const float kM[NUM_MODES][12] = {
      {0.00f, 0.45f, 0.20f, 0.70f, 7.f, 1.5f, 0.00025f, 1.00f, 0.00f, 0.6f, 1.0f, 1.00f},  // TAPE
      {1.00f, 0.60f, 0.50f, 0.35f, 2.f, 0.3f, 0.00040f, 1.00f, 0.30f, 0.6f, 1.2f, 1.00f},  // DUB
      {0.00f, 0.02f, 0.02f, 0.00f, 0.f, 0.0f, 0.00300f, 1.00f, 0.00f, 0.6f, 1.0f, 0.95f},  // DIGI
      {0.35f, 0.35f, 0.15f, 0.20f, 3.f, 0.5f, 0.00050f, 0.75f, 0.75f, 1.3f, 4.5f, 0.85f},  // SPACE
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_TAPE];
    const float t = dsp::kNoteBeats[dsp::noteIndex(p.time)] * beatSamples_;

    dsp::StereoDelay::Config c;
    c.timeL = t;
    c.timeR = t * m[7];
    c.feedback = p.feedback * 1.15f;
    c.cross = m[0]; c.hiCut = m[1]; c.loCut = m[2]; c.drive = m[3];
    c.wow = m[4]; c.flutter = m[5]; c.glide = m[6];
    dly_.setConfig(c);
    const float send = m[8], trim = m[11];
    if (send > 0.f) {
      dsp::Reverb::Config r;
      r.size = m[9]; r.decay = m[10]; r.damp = 0.45f; r.diffusion = 0.7f; r.modDepth = 6.f;
      verb_.setConfig(r);
    }

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 1.000f, 1.096f};  // @auto-level

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
