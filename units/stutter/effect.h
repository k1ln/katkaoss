#pragma once
// Stutter — tempo-synced beat repeat.
// X=REPEATS  Y=GATE  DEPTH=mix  MODE=QTR/8TH/16TH/ROLL  DRIVE=grit
//
// Every catch grabs the last slice of audio (one note value long, following
// the NTS-3 tempo) and loops it; X sets how many times a slice repeats before
// the next catch (1 = always fresh, 8 = long hold), Y how much of each repeat
// sounds (short = choppy gate). Touching the pad punches in: it catches right
// away and restarts the grid from your touch.
//
// (Previously this ran a grain cloud with freeze hard-wired on, so its buffer
// never recorded anything and every mode was silent.)
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }
  enum { REPEATS = 0U, GATE, DEPTH, MODE, NUM_PARAMS };
  enum { M_QTR = 0, M_8TH, M_16TH, M_ROLL, NUM_MODES };
  struct Params { float position = 0.5f, size = 0.5f, depth = 0.f; uint32_t mode = M_8TH; };

  inline void setParameter(uint8_t i, int32_t v) override final {
    switch (i) {
      case REPEATS: params_.position = param_10bit_to_f32(v); break;
      case GATE: params_.size = param_10bit_to_f32(v); break;
      case DEPTH: params_.depth = v / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(v); break;
      case MODE: params_.mode = v; break;
    }
  }
  inline const char *getParameterStrValue(uint8_t i, int32_t v) const override final {
    static const char *m[NUM_MODES] = {"QTR", "8TH", "16TH", "ROLL"};
    if (i == MODE && v >= 0 && v < NUM_MODES) return m[v];
    return nullptr;
  }
  void init(float *b) override final {
    alloc_.init(b, getBufferSize());
    rep_.init(alloc_.alloc(240000), 240000);  // 5 s: a QTR slice held 8x at 120 BPM
    params_ = Params();
    t_ = 0; slice_ = 0; recorded_ = 0; beatSamples_ = 24000.f; punch_ = false;
    resync_ = false;
  }
  void teardown() override final {}
  void reset() override final {}

  void setTempo(float bpm) override final {
    if (bpm >= 20.f && bpm <= 400.f) beatSamples_ = 60.f / bpm * dsp::kSampleRate;
  }
  // Snap the grid to the host's quarter notes so catches land on the beat.
  void tempo4ppqnTick(uint32_t counter) override final {
    if ((counter & 3U) == 0U) resync_ = true;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    //                     division  gateMin  rate   trim
    static const float kM[NUM_MODES][4] = {
      {1.000f, 0.50f, 1.00f, 1.00f},  // QTR   one beat
      {0.500f, 0.40f, 1.00f, 1.00f},  // 8TH
      {0.250f, 0.30f, 1.00f, 1.05f},  // 16TH
      {0.125f, 0.20f, 1.00f, 1.10f},  // ROLL  32nds, tight gate
    };
    const float *m = kM[p.mode < NUM_MODES ? p.mode : M_8TH];
    const uint32_t sliceLen = (uint32_t)(beatSamples_ * m[0]);
    const uint32_t repeats = 1U + (uint32_t)(p.position * 7.99f);   // 1..8
    const float gate = m[1] + (1.f - m[1]) * p.size;
    const float mix = (p.depth + 1.f) * 0.5f;
    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).
    static const float kLevel[NUM_MODES] = {1.135f, 1.175f, 1.189f, 1.245f};  // @auto-level
    const float lvl = kLevel[(p.mode < NUM_MODES ? p.mode : 0)];

    if (resync_) {  // round the running clock to the nearest beat
      const float beats = (float)t_ / beatSamples_;
      t_ = (uint32_t)((float)(uint32_t)(beats + 0.5f) * beatSamples_);
      resync_ = false;
    }
    for (const float *e = out + frames * 2; out != e; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      if (punch_) { t_ = 0; punch_ = false; }
      if (sliceLen > 0 && t_ % sliceLen == 0) {
        if (slice_ % repeats == 0) {                               // fresh catch
          if (recorded_ >= sliceLen) rep_.trigger((float)sliceLen);
          else rep_.release();  // not a full slice of audio yet: pass through
        }
        ++slice_;
      }
      ++t_;
      if (recorded_ < 0x7fffffffU) ++recorded_;
      const float w = rep_.process(dry, m[2], gate) * m[3];
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
  Params params_; float mDrive_ = 0.f;
  uint32_t t_ = 0, slice_ = 0, recorded_ = 0;
  float beatSamples_ = 24000.f;  // 120 BPM until the host tells us otherwise
  volatile bool punch_ = false, resync_ = false;
};
