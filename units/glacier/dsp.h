#pragma once
/*
 *  dsp.h — shared DSP building blocks for KatKaoss NTS-3 units.
 *
 *  Header-only. All buffers are sub-allocated from the unit's SDRAM block
 *  (passed to Effect::init) via BufferAllocator, so nothing here allocates
 *  on the heap.
 */
#include <cstdint>
#include <cmath>

namespace dsp {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kSampleRate = 48000.f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float softclip(float x) { return tanhf(x); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Bump allocator over the SDRAM buffer handed to the unit.
class BufferAllocator {
 public:
  void init(float *base, uint32_t size) {
    base_ = base;
    size_ = size;
    used_ = 0;
    for (uint32_t i = 0; i < size_; ++i) base_[i] = 0.f;
  }
  float *alloc(uint32_t n) {
    if (!base_ || used_ + n > size_) return nullptr;
    float *p = base_ + used_;
    used_ += n;
    return p;
  }
  uint32_t remaining() const { return size_ - used_; }

 private:
  float *base_ = nullptr;
  uint32_t size_ = 0;
  uint32_t used_ = 0;
};

// Fractional-read delay line / circular buffer.
class DelayLine {
 public:
  void init(float *buf, uint32_t size) {
    buf_ = buf;
    size_ = size;
    w_ = 0;
    if (buf_)
      for (uint32_t i = 0; i < size_; ++i) buf_[i] = 0.f;
  }
  inline void write(float x) {
    buf_[w_] = x;
    if (++w_ >= size_) w_ = 0;
  }
  inline float readInt(uint32_t d) const {
    if (d >= size_) d = size_ - 1;
    uint32_t i = (w_ + size_ - d) % size_;
    return buf_[i];
  }
  // read `d` samples in the past, linearly interpolated (d >= 1).
  inline float read(float d) const {
    if (d < 1.f) d = 1.f;
    if (d > (float)(size_ - 2)) d = (float)(size_ - 2);
    uint32_t di = (uint32_t)d;
    float f = d - (float)di;
    uint32_t i0 = (w_ + size_ - di) % size_;
    uint32_t i1 = (i0 + size_ - 1) % size_;
    return buf_[i0] + (buf_[i1] - buf_[i0]) * f;
  }
  uint32_t size() const { return size_; }
  bool valid() const { return buf_ != nullptr; }

 private:
  float *buf_ = nullptr;
  uint32_t size_ = 0;
  uint32_t w_ = 0;
};

// One-pole low/high pass.
class OnePole {
 public:
  inline float lp(float x, float coeff) {
    z_ += coeff * (x - z_);
    return z_;
  }
  inline float hp(float x, float coeff) { return x - lp(x, coeff); }
  void reset() { z_ = 0.f; }

 private:
  float z_ = 0.f;
};

// Sine LFO, phase in [0,1).
class LFO {
 public:
  inline float next(float inc) {
    ph_ += inc;
    if (ph_ >= 1.f) ph_ -= 1.f;
    return sinf(kTwoPi * ph_);
  }
  // triangle output in [-1,1]
  inline float nextTri(float inc) {
    ph_ += inc;
    if (ph_ >= 1.f) ph_ -= 1.f;
    return 4.f * fabsf(ph_ - 0.5f) - 1.f;
  }
  void setPhase(float p) { ph_ = p; }
  float phase() const { return ph_; }

 private:
  float ph_ = 0.f;
};

// Freeverb-style damped comb.
class Comb {
 public:
  void init(float *buf, uint32_t size) {
    buf_ = buf;
    size_ = size;
    idx_ = 0;
    store_ = 0.f;
    if (buf_)
      for (uint32_t i = 0; i < size_; ++i) buf_[i] = 0.f;
  }
  inline float process(float in, float feedback, float damp) {
    float out = buf_[idx_];
    store_ = out * (1.f - damp) + store_ * damp;
    buf_[idx_] = in + store_ * feedback;
    if (++idx_ >= size_) idx_ = 0;
    return out;
  }

 private:
  float *buf_ = nullptr;
  uint32_t size_ = 0;
  uint32_t idx_ = 0;
  float store_ = 0.f;
};

// Freeverb-style allpass.
class Allpass {
 public:
  void init(float *buf, uint32_t size) {
    buf_ = buf;
    size_ = size;
    idx_ = 0;
    if (buf_)
      for (uint32_t i = 0; i < size_; ++i) buf_[i] = 0.f;
  }
  inline float process(float in, float feedback) {
    float bufout = buf_[idx_];
    float out = -in + bufout;
    buf_[idx_] = in + bufout * feedback;
    if (++idx_ >= size_) idx_ = 0;
    return out;
  }

 private:
  float *buf_ = nullptr;
  uint32_t size_ = 0;
  uint32_t idx_ = 0;
};

// Stereo Freeverb (8 combs + 4 allpass per channel).
class Reverb {
 public:
  static constexpr int kNumCombs = 8;
  static constexpr int kNumAllpass = 4;

  bool init(BufferAllocator &a) {
    static const uint32_t comb[kNumCombs] = {1116, 1188, 1277, 1356,
                                             1422, 1491, 1557, 1617};
    static const uint32_t allp[kNumAllpass] = {556, 441, 341, 225};
    const uint32_t spread = 23;
    for (int i = 0; i < kNumCombs; ++i) {
      float *bl = a.alloc(comb[i]);
      float *br = a.alloc(comb[i] + spread);
      if (!bl || !br) return false;
      combL_[i].init(bl, comb[i]);
      combR_[i].init(br, comb[i] + spread);
    }
    for (int i = 0; i < kNumAllpass; ++i) {
      float *bl = a.alloc(allp[i]);
      float *br = a.alloc(allp[i] + spread);
      if (!bl || !br) return false;
      apL_[i].init(bl, allp[i]);
      apR_[i].init(br, allp[i] + spread);
    }
    return true;
  }

  // roomsize/damp in [0,1]. extraFeed lets callers push into shimmer territory.
  inline void process(float inL, float inR, float &outL, float &outR,
                      float roomsize, float damp, float extraFeed = 0.f) {
    const float fb = clampf(0.7f + roomsize * 0.28f + extraFeed, 0.f, 0.998f);
    const float d = clampf(damp * 0.4f, 0.f, 0.4f);
    const float input = (inL + inR) * 0.5f * 0.015f;
    float accL = 0.f, accR = 0.f;
    for (int i = 0; i < kNumCombs; ++i) {
      accL += combL_[i].process(input, fb, d);
      accR += combR_[i].process(input, fb, d);
    }
    for (int i = 0; i < kNumAllpass; ++i) {
      accL = apL_[i].process(accL, 0.5f);
      accR = apR_[i].process(accR, 0.5f);
    }
    outL = accL * 3.0f;
    outR = accR * 3.0f;
  }

 private:
  Comb combL_[kNumCombs], combR_[kNumCombs];
  Allpass apL_[kNumAllpass], apR_[kNumAllpass];
};

// Delay-line granular pitch shifter (two crossfaded read heads).
class PitchShifter {
 public:
  void init(float *buf, uint32_t size) {
    d_.init(buf, size);
    grain_ = (float)(size / 2 - 4);
    if (grain_ > 3600.f) grain_ = 3600.f;  // ~75ms grains
    phase_ = 0.f;
  }
  // ratio: 2 = +1 oct, 0.5 = -1 oct.
  inline float process(float in, float ratio) {
    d_.write(in);
    phase_ += (ratio - 1.f);
    while (phase_ >= grain_) phase_ -= grain_;
    while (phase_ < 0.f) phase_ += grain_;
    float p2 = phase_ + grain_ * 0.5f;
    if (p2 >= grain_) p2 -= grain_;
    float env1 = 0.5f * (1.f - cosf(kTwoPi * phase_ / grain_));
    float env2 = 0.5f * (1.f - cosf(kTwoPi * p2 / grain_));
    float s1 = d_.read(phase_ + 1.f);
    float s2 = d_.read(p2 + 1.f);
    return s1 * env1 + s2 * env2;
  }

 private:
  DelayLine d_;
  float grain_ = 2400.f;
  float phase_ = 0.f;
};

// State-variable filter (Chamberlin). cutoff in [0,1] ~ fraction of Nyquist.
class SVF {
 public:
  void reset() { lp_ = bp_ = 0.f; }
  inline void process(float x, float cutoff, float res, float &lp, float &bp,
                      float &hp) {
    float f = 2.f * sinf(kPi * clampf(cutoff, 0.0004f, 0.45f));
    float q = 1.f - clampf(res, 0.f, 0.96f);
    hp = x - lp_ - q * bp_;
    bp_ += f * hp;
    lp_ += f * bp_;
    lp = lp_;
    bp = bp_;
  }
  // convenience overload when the high-pass output is not needed
  inline void process(float x, float cutoff, float res, float &lp, float &bp) {
    float hp;
    process(x, cutoff, res, lp, bp, hp);
  }

 private:
  float lp_ = 0.f, bp_ = 0.f;
};

// Feedback delay with damping in the feedback path.
class FBDelay {
 public:
  void init(float *buf, uint32_t size) {
    d_.init(buf, size);
    lp_ = 0.f;
  }
  inline float process(float in, float timeSamples, float feedback, float damp) {
    float y = d_.read(timeSamples);
    lp_ += damp * (y - lp_);
    d_.write(in + lp_ * feedback);
    return y;
  }
  inline float tap(float timeSamples) const { return d_.read(timeSamples); }
  uint32_t size() const { return d_.size(); }

 private:
  DelayLine d_;
  float lp_ = 0.f;
};

// Free-running sine oscillator (for ring mod / tremolo).
class Osc {
 public:
  inline float sine(float hz) {
    ph_ += hz / kSampleRate;
    if (ph_ >= 1.f) ph_ -= 1.f;
    return sinf(kTwoPi * ph_);
  }
  void reset() { ph_ = 0.f; }

 private:
  float ph_ = 0.f;
};

// Symmetric wavefolder.
inline float wavefold(float x, float drive) {
  x *= drive;
  for (int i = 0; i < 6; ++i) {
    if (x > 1.f)
      x = 2.f - x;
    else if (x < -1.f)
      x = -2.f - x;
    else
      break;
  }
  return x;
}

// Overdrive/grit saturator. amount in [0,1]:
//  ~0    -> nearly clean, 0.25 -> warm saturation, 1.0 -> brutal fuzz/fold.
inline float grit(float x, float amount) {
  if (amount <= 0.0005f) return x;
  float pre = 1.f + amount * amount * 45.f;  // gentle low, savage high
  float y = tanhf(x * pre);
  if (amount > 0.45f) {  // add wavefold bite when pushed hard
    float f = wavefold(x * (1.f + amount * 7.f), 1.f);
    float m = (amount - 0.45f) * 1.8f;  // 0..1
    y = lerp(y, f, 0.35f * (m > 1.f ? 1.f : m));
  }
  float makeup = 0.8f + amount * 0.35f;
  y *= makeup;
  return y < -1.6f ? -1.6f : (y > 1.6f ? 1.6f : y);
}

// Apply grit to the wet signal, then dry/wet blend. Used by every unit's output.
inline float driveMix(float dry, float drive, float wet, float mix) {
  return lerp(dry, grit(wet, drive), mix);
}

// Cheap xorshift RNG -> [0,1).
class Rng {
 public:
  inline float next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return (float)(s_ & 0xFFFFFF) * (1.f / 16777216.f);
  }

 private:
  uint32_t s_ = 0x1234abcdu;
};

// Polyphonic granular engine over a recorded buffer (Clouds/Grain/Freeze).
class GrainCloud {
 public:
  static constexpr int kMaxGrains = 32;  // max simultaneous grains ("ripples")

  void init(float *buf, uint32_t size) {
    rec_.init(buf, size);
    timer_ = 0;
    for (int i = 0; i < kMaxGrains; ++i) g_[i].active = false;
  }

  // density,size,spread,position in [0,1]; pitch is a ratio; freeze stops recording.
  inline void process(float in, float density, float grainSize, float pitch,
                      float spread, float position, bool freeze, float &outL,
                      float &outR) {
    if (!freeze) rec_.write(in);

    const float durSamples = 480.f + grainSize * grainSize * 9600.f;  // 10ms..~200ms
    if (timer_ <= 0.f) {
      spawn(durSamples, pitch, spread, position);
      float interval = durSamples / (0.6f + density * 14.f);  // denser grain stream
      timer_ = interval < 1.f ? 1.f : interval;
    }
    timer_ -= 1.f;

    float l = 0.f, r = 0.f;
    const uint32_t maxDelay = rec_.size() - 2;
    for (int i = 0; i < kMaxGrains; ++i) {
      Grain &gr = g_[i];
      if (!gr.active) continue;
      float env = 0.5f * (1.f - cosf(kTwoPi * (gr.t / gr.dur)));
      float s = rec_.read(gr.pos);
      l += s * env * gr.panL;
      r += s * env * gr.panR;
      gr.pos -= gr.inc;
      if (gr.pos < 1.f) gr.pos += (float)maxDelay;
      if (gr.pos > (float)maxDelay) gr.pos -= (float)maxDelay;
      gr.t += 1.f;
      if (gr.t >= gr.dur) gr.active = false;
    }
    outL = l * 0.5f;
    outR = r * 0.5f;
  }

 private:
  struct Grain {
    bool active;
    float pos, t, dur, inc, panL, panR;
  };

  void spawn(float dur, float pitch, float spread, float position) {
    for (int i = 0; i < kMaxGrains; ++i) {
      if (g_[i].active) continue;
      const uint32_t maxDelay = rec_.size() - 2;
      float base = position * (float)(maxDelay - 4800) + 2400.f;
      float jitter = (rng_.next() - 0.5f) * spread * (float)(maxDelay) * 0.5f;
      float pos = base + jitter;
      if (pos < 1.f) pos = 1.f;
      if (pos > (float)maxDelay) pos = (float)maxDelay;
      float pan = rng_.next() * spread;
      g_[i].active = true;
      g_[i].pos = pos;
      g_[i].t = 0.f;
      g_[i].dur = dur;
      g_[i].inc = pitch;
      g_[i].panL = 0.5f + 0.5f * (1.f - pan);
      g_[i].panR = 0.5f + 0.5f * pan;
      return;
    }
  }

  DelayLine rec_;
  Grain g_[kMaxGrains];
  float timer_ = 0.f;
  Rng rng_;
};

}  // namespace dsp
