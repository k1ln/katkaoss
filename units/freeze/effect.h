#pragma once
/*
 *  File: effect.h — Freeze
 *
 *  Capture the incoming sound and sustain it forever as a grain cloud.
 *  X = POSITION (where in the captured sound the grains play; GLIDE: drift speed)
 *  Y = SIZE     (grain size / smoothness, and tail length)
 *  DEPTH = dry/wet
 *  CATCH = LIVE / FREEZE / SMEAR / GLIDE
 *  DRIVE = grit
 *
 *  Catching: touching the pad grabs fresh sound (drag X to scrub through it,
 *  release and it stays held). Switching CATCH into a hold mode also grabs.
 *  A hold only locks once there is real sound in the buffer, so loading the
 *  unit in FREEZE with nothing playing waits for input rather than holding
 *  silence (the old behaviour: three of the four modes were permanently mute).
 *  Held sound is level-matched to what was playing when it was caught, so
 *  landing X in the decay between two notes doesn't give a near-silent hold.
 *
 *  LIVE   grains follow the input, small room
 *  FREEZE tight, stable hold — a clean sustained chord, medium hall
 *  SMEAR  the held sound blurred across the whole capture into a dark wash
 *  GLIDE  the play point drifts through the capture, bright shimmering hall
 *
 *  Extra knobs (edit menu, assignable to X/Y): SHAPE grain envelope, SCATTER
 *  per-grain pitch/size/timing randomness, REVERSE share of backwards grains —
 *  all acting on the held sound. Centred/zero they leave FREEZE as it was.
 *  @param 5 SHAPE 0 1023 512
 *  @param 6 SCATTER 0 1023 0
 *  @param 7 REVERSE 0 1023 0
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "dsp.h"

class Effect : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0x40000U; }

  enum { POSITION = 0U, SIZE, DEPTH, CATCH, NUM_PARAMS };
  enum { C_LIVE = 0, C_FREEZE, C_SMEAR, C_GLIDE, NUM_CATCH };

  struct Params {
    float position = 0.5f;
    float size = 0.5f;
    float depth = 0.f;
    uint32_t catchMode = C_FREEZE;
  };

  inline void setParameter(uint8_t index, int32_t value) override final {
    if (knobs_.set(index, value)) return;
    switch (index) {
      case POSITION: params_.position = param_10bit_to_f32(value); break;
      case SIZE: params_.size = param_10bit_to_f32(value); break;
      case DEPTH: params_.depth = value / 1000.f; break;
      case 4: mDrive_ = param_10bit_to_f32(value); break;
      case CATCH: params_.catchMode = value; break;
      default: break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *c[NUM_CATCH] = {"LIVE", "FREEZE", "SMEAR", "GLIDE"};
    if (index == CATCH && value >= 0 && value < NUM_CATCH) return c[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final {
    alloc_.init(allocated_buffer, getBufferSize());
    cloud_.init(alloc_.alloc(96000), 96000);
    cloud_.setCaptureMin(28800);  // 0.6 s of sound, so a touch-grab feels immediate
    knobs_.reset();
    verb_.init(alloc_, 2.6f, 0.04f);
    params_ = Params();
    glide_ = 0.f;
    lastMode_ = NUM_CATCH;        // forces a capture on the first block
    grab_ = false;
    inPow_ = wetPow_ = targetPow_ = 0.f;
    agc_ = 1.f;
    wasHeld_ = false;
  }

  void teardown() override final {}
  void reset() override final {}

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final {
    const Params p = params_;
    const uint32_t mode = p.catchMode < NUM_CATCH ? p.catchMode : C_FREEZE;
    const bool hold = (mode != C_LIVE);
    if (grab_ || (hold && mode != lastMode_)) cloud_.recapture();
    grab_ = false;
    lastMode_ = mode;

    //                   density spread  rvSize rvDecay damp  mod   send  wetTrim
    static const float kM[NUM_CATCH][8] = {
      {0.55f, 0.30f, 0.70f,  1.2f, 0.45f,  4.f, 0.35f, 0.80f},  // LIVE
      {0.90f, 0.25f, 1.30f,  3.5f, 0.35f,  5.f, 0.55f, 0.46f},  // FREEZE
      {1.00f, 0.95f, 2.40f, 11.0f, 0.70f, 10.f, 1.00f, 0.38f},  // SMEAR
      {0.80f, 0.40f, 1.80f,  6.0f, 0.12f, 14.f, 0.80f, 0.33f},  // GLIDE
    };
    const float *m = kM[mode];

    // GLIDE: X becomes drift speed (~40 s per sweep at 0 .. ~2 s at 1)
    float pos = p.position;
    if (mode == C_GLIDE) {
      glide_ += (float)frames * (0.5f + p.position * 10.f) / (40.f * dsp::kSampleRate);
      if (glide_ >= 1.f) glide_ -= 1.f;
      pos = glide_ < 0.5f ? glide_ * 2.f : 2.f - glide_ * 2.f;  // ping-pong, no seam
    }
    const float gsize = mode == C_SMEAR ? 0.5f + p.size * 0.5f : p.size;
    cloud_.setTexture(knobs_.apply(dsp::GrainCloud::Texture()));

    dsp::Reverb::Config c;
    c.size = m[2];
    c.decay = m[3] * (0.4f + p.size * 1.2f);
    c.damp = m[4];
    c.modDepth = m[5];
    c.modRate = 0.4f;
    c.diffusion = 0.75f;
    c.lowCut = 0.15f;
    c.preDelay = 0.015f;
    verb_.setConfig(c);

    const float mix = (p.depth + 1.f) * 0.5f;

    // Per-mode output level, written by scripts/calibrate_levels.py (wet ~ dry on pink noise).

    static const float kLevel[NUM_CATCH] = {1.259f, 1.995f, 1.862f, 2.512f};  // @auto-level

    const float lvl = kLevel[mode];
    const float send = m[6], trim = m[7];
    for (const float *end = out + frames * 2; out != end; in += 2, out += 2) {
      const float dry = (in[0] + in[1]) * 0.5f;
      float gl, gr;
      cloud_.process(dry, m[0], gsize, 1.f, m[1], pos, hold, gl, gr);

      // Level match: while recording, track the input level; at the moment the
      // hold locks, that becomes the target the held grains are steered to.
      const bool held = cloud_.held();
      if (!held) inPow_ += 0.00007f * (dry * dry - inPow_);           // ~300 ms
      if (held && !wasHeld_) targetPow_ = inPow_;
      wasHeld_ = held;
      wetPow_ += 0.00007f * ((gl * gl + gr * gr) * 0.5f - wetPow_);
      float g = 1.f;
      if (held && wetPow_ > 1e-9f) g = dsp::clampf(sqrtf(targetPow_ / wetPow_), 0.3f, 6.f);
      agc_ += 0.0005f * (g - agc_);                                    // no zipper
      gl *= agc_;
      gr *= agc_;

      float rl, rr;
      verb_.process(gl, gr, rl, rr);
      out[0] = dsp::driveMix(in[0], mDrive_, gl + rl * send, mix, trim * lvl);
      out[1] = dsp::driveMix(in[1], mDrive_, gr + rr * send, mix, trim * lvl);
    }
  }

  // Touching the pad catches fresh sound. Only the start of a touch counts, so
  // dragging X afterwards scrubs through what was just caught.
  inline void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final {
    if (phase == k_unit_touch_phase_began) grab_ = true;
  }

 private:
  dsp::BufferAllocator alloc_;
  dsp::GrainCloud cloud_;
  dsp::Reverb verb_;
  dsp::GrainKnobs knobs_;
  Params params_; float mDrive_ = 0.f;
  float glide_ = 0.f;
  float inPow_ = 0.f, wetPow_ = 0.f, targetPow_ = 0.f, agc_ = 1.f;
  bool wasHeld_ = false;
  uint32_t lastMode_ = NUM_CATCH;
  volatile bool grab_ = false;  // set from the touch callback, consumed per block
};
