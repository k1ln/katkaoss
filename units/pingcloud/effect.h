#pragma once
// PingCloud — ping-pong delay whose echoes get granulated (free time).
// X=TIME (125 ms .. 1 s)  Y=FEEDBACK  DEPTH=mix  MODE=WIDE/DUB/GRAIN/INFIN  DRIVE=grit
//   WIDE   clean ping-pong with a light grain halo    DUB    dark band-limited ping-pong, no grains
//   GRAIN  the echoes dissolve into a grain cloud     INFIN  runaway feedback + grains: endless
//
// Extra knobs (NTS-3 edit menu, assignable to X/Y): SHAPE grain envelope
// (percussive <-> gated, centre = the mode's own), SCATTER per-grain
// pitch/size/timing randomness, REVERSE share of backwards grains.
// @param 5 SHAPE 0 1023 512
// @param 6 SCATTER 0 1023 0
// @param 7 REVERSE 0 1023 0
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { TIME = 0U, FEEDBACK, DEPTH, MODE, NUM_PARAMS };
  enum { M_WIDE = 0, M_DUB, M_GRAIN, M_INFIN, NUM_MODES };
  struct Params { float time = 0.5f, feedback = 0.5f, depth = 0.f; uint32_t mode = M_WIDE; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    if (knobs_.set(i, v)) return;
    switch (i) {
      case TIME: params_.time = param_10bit_to_f32(v); break;
      case FEEDBACK: params_.feedback = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"WIDE", "DUB", "GRAIN", "INFIN"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    dly_.init(alloc_, 1.05f, false);
    cloud_.init(alloc_.alloc(96000), 96000);
    knobs_.reset();
    params_ = Params();
  }
  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.mode < NUM_MODES ? p.mode : M_WIDE;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.000f, 1.000f, 1.862f, 1.216f};  // @auto-level
    const float lvl = kLevel[mode];
    static const float kT[NUM_MODES][7] = {
      {0.50f, 0.f, 0.f, 0.0f, 0.3f, 0.3f, 1.f},  // WIDE
      {0.50f, 0.f, 0.f, 0.0f, 0.3f, 0.3f, 1.f},  // DUB
      {0.40f, 5.f, 1.f, 0.2f, 0.5f, 0.7f, 1.f},  // GRAIN
      {0.70f, 0.f, 0.f, 0.1f, 0.4f, 0.4f, 1.f},  // INFIN
    };
    //                   fbMax  hiCut loCut  grains echoLvl
    static const float kD[NUM_MODES][5] = {
      {0.85f, 0.15f, 0.10f, 0.25f, 1.0f},
      {0.92f, 0.60f, 0.45f, 0.00f, 1.0f},
      {0.85f, 0.25f, 0.15f, 1.00f, 0.4f},
      {1.10f, 0.35f, 0.15f, 0.50f, 0.8f},
    };
    const float *d = kD[mode];
    cloud_.setTexture(knobs_.apply(dsp::texture(kT[mode])));
    dsp::StereoDelay::Config c;
    c.timeL = c.timeR = 6000.f + p.time * 42000.f;
    c.feedback = p.feedback * d[0]; c.cross = 1.f; c.hiCut = d[1]; c.loCut = d[2]; c.glide = 0.0008f;
    dly_.setConfig(c);
    const float grains = d[3], echo = d[4];
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      float yl, yr; dly_.process(in[0], in[1], yl, yr);
      float wl = yl * echo, wr = yr * echo;
      if (grains > 0.f) {
        float gl, gr; cloud_.process((yl + yr) * 0.5f, 0.6f, 0.45f, 1.f, 0.7f, 0.5f, false, gl, gr);
        wl += gl * grains; wr += gr * grains;
      }
      out[0] = dsp::driveMix(in[0], mDrive_, wl, mix, lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, wr, mix, lvl);
    }
  }
  inline void touchEvent(uint8_t, uint8_t, uint32_t, uint32_t) override final {}

 private:
  dsp::BufferAllocator alloc_;
  dsp::StereoDelay dly_;
  dsp::GrainCloud cloud_;
  dsp::GrainKnobs knobs_;
  Params params_;
  float mDrive_ = 0.f;
};
