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
// sin(2*pi*ph) for ph in [0,1), ~0.1% error, no libm call.
inline float fastSin01(float ph) {
  float x = ph * 2.f - 1.f;
  float y = 4.f * x * (1.f - fabsf(x));
  return y * (0.775f + 0.225f * fabsf(y));
}

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
    uint32_t i0 = w_ + size_ - di;
    if (i0 >= size_) i0 -= size_;
    uint32_t i1 = (i0 == 0) ? size_ - 1 : i0 - 1;
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

// Schroeder allpass: y = -g*v + v[n-D], v = x + g*v[n-D], i.e.
// H(z) = (z^-D - g) / (1 - g z^-D) — flat magnitude at every g.
// (Freeverb's version, out = buf - in, is not actually allpass: its gain runs
// from g/(1-g) at DC to (2+g)/(1+g) at Nyquist, so raising the diffusion
// coefficient also raised the reverb's level.)
class Allpass {
 public:
  void init(float *buf, uint32_t size) {
    buf_ = buf;
    size_ = size;
    idx_ = 0;
    if (buf_)
      for (uint32_t i = 0; i < size_; ++i) buf_[i] = 0.f;
  }
  inline float process(float in, float g) {
    const float vd = buf_[idx_];  // v[n-D]
    const float v = in + g * vd;
    buf_[idx_] = v;
    if (++idx_ >= size_) idx_ = 0;
    return vd - g * v;
  }

 private:
  float *buf_ = nullptr;
  uint32_t size_ = 0;
  uint32_t idx_ = 0;
};

// Cascaded first-order allpass chain — frequency-dependent delay ("boing").
// This is what makes a spring tank sound like a spring rather than a room.
// N is the maximum stage count; `stages` picks how many actually run, so one
// instance can be a lazy 4-stage wobble or a full 24-stage spring tank.
template <int N>
class Disperser {
 public:
  void reset() {
    for (int i = 0; i < N; ++i) x1_[i] = y1_[i] = 0.f;
  }
  inline float process(float x, float a, int stages) {
    if (stages > N) stages = N;
    for (int i = 0; i < stages; ++i) {
      const float y = -a * x + x1_[i] + a * y1_[i];
      x1_[i] = x;
      y1_[i] = y;
      x = y;
    }
    return x;
  }

 private:
  float x1_[N] = {0}, y1_[N] = {0};
};

// Reverb delay lengths (samples @48k) at size == 1.0. Mutually prime, spread
// ~3.2:1 so the modal series of the lines don't line up and ring.
// (Namespace scope rather than static constexpr members: this builds as C++11,
// where an ODR-used in-class constexpr array still needs an out-of-class
// definition.)
static const uint32_t kRvBaseLen[8] = {1187, 1523, 1867, 2203,
                                       2647, 3011, 3389, 3821};
// Input diffusion allpass lengths — fixed (these set echo density, not room
// size, so they deliberately do not scale with `size`).
static const uint32_t kRvDiffLen[4] = {142, 379, 107, 277};

// ---------------------------------------------------------------------------
// Reverb — modulated 8-line feedback delay network (Jot/Stautner-Puckette
// style) with an input diffusion stage, pre-delay and decay-independent
// output normalisation.
//
// Why not Freeverb: in a comb-filter reverb "room size" is only feedback, so
// every size shares one set of comb tunings and therefore one resonant
// fingerprint — small rooms and cathedrals sound like the same box at
// different decay times. Here `size` scales the actual delay lengths, which
// moves the modal density, and `decay` sets RT60 independently. Level is
// normalised against decay so a longer tail does not also mean a louder one.
// ---------------------------------------------------------------------------
class Reverb {
 public:
  static constexpr int kLines = 8;
  static constexpr int kDiffusers = 4;

  struct Config {
    float size = 1.0f;        // delay-length scale; clamped to [0.2, maxSize]
    float decay = 2.0f;       // RT60 in seconds
    float damp = 0.35f;       // 0..1 HF absorption in the loop
    float lowCut = 0.08f;     // 0..1 LF absorption in the loop
    float diffusion = 0.72f;  // 0..1 input allpass coefficient
    float modDepth = 6.f;     // tail modulation, in samples
    float modRate = 0.7f;     // tail modulation rate, Hz
    float preDelay = 0.f;     // seconds (needs maxPreDelay at init)
    float width = 1.f;        // 0..1 stereo spread
    // Dispersion inside the feedback loop: frequency-dependent delay, so each
    // recirculation smears further and a transient turns into a descending
    // chirp. 0 stages = off (and costs nothing). This is the spring-tank knob.
    int dispStages = 0;       // 0..24, applied to 2 of the 8 lines
    float dispCoef = 0.72f;   // allpass coefficient, 0..0.93
  };

  // maxSize/maxPreDelay set how much SDRAM is claimed. Defaults match the
  // legacy call site `init(alloc)`.
  bool init(BufferAllocator &a, float maxSize = 2.0f, float maxPreDelay = 0.f) {
    maxSize_ = maxSize < 0.25f ? 0.25f : maxSize;
    for (int i = 0; i < kLines; ++i) {
      uint32_t n = (uint32_t)((float)kRvBaseLen[i] * maxSize_) + 8;
      float *b = a.alloc(n);
      if (!b) return false;
      line_[i].init(b, n);
      maxLen_[i] = (float)(n - 4);
      lpz_[i] = hpz_[i] = 0.f;
      // Spread the modulation phases and rates so the lines never pump in step.
      modPh_[i] = (float)i * 0.125f;
      modMul_[i] = 0.7f + 0.13f * (float)i;
      modOff_[i] = 0.f;
    }
    for (int i = 0; i < kDiffusers; ++i) {
      float *b = a.alloc(kRvDiffLen[i]);
      if (!b) return false;
      diff_[i].init(b, kRvDiffLen[i]);
    }
    disp_[0].reset();
    disp_[1].reset();
    // Forget the legacy call's cached arguments: init() just reset the config,
    // so the next legacy process() must re-apply its (room, damp, extra) even if
    // they match the values from before the re-init.
    lRoom_ = lDamp_ = lExtra_ = -1.f;
    preMax_ = (uint32_t)(maxPreDelay * kSampleRate);
    if (preMax_ > 0) {
      preMax_ += 4;
      float *b = a.alloc(preMax_);
      if (!b) return false;
      pre_.init(b, preMax_);
    }
    modCount_ = 0;
    setConfig(Config());
    return true;
  }

  // Call once per audio block, not per sample (it uses expf).
  void setConfig(const Config &c) {
    const float size = clampf(c.size, 0.2f, maxSize_);
    const float decay = clampf(c.decay, 0.05f, 120.f);
    float gsum = 0.f;
    for (int i = 0; i < kLines; ++i) {
      float d = (float)kRvBaseLen[i] * size;
      if (d > maxLen_[i]) d = maxLen_[i];
      if (d < 8.f) d = 8.f;
      delay_[i] = d;
      // RT60: -60 dB after `decay` seconds => g = 10^(-3*d/(decay*SR)).
      float g = expf(-6.907755f * d / (decay * kSampleRate));
      if (g > 0.9995f) g = 0.9995f;
      g_[i] = g;
      gsum += g;
    }
    const float gm = gsum * (1.f / (float)kLines);
    // Output trim so wet level stays put as the tail gets longer: a slower
    // decay stores more energy in the network, exactly by 1/(1-g^2).
    outGain_ = kNorm_ * sqrtf(2.f * (1.f - gm * gm));
    // damp is "how much HF the room absorbs", so it has to *lower* the
    // one-pole coefficient: 0 -> transparent, 1 -> ~140 Hz wall of felt.
    dampCoef_ = expf(-clampf(c.damp, 0.f, 1.f) * 4.f);
    // A one-pole LP passes a/(2-a) of *white*-noise power, but music is mostly
    // lows and mids, which damping barely touches. Calibrated on pink noise, a
    // small makeup keeps the level flat across the tone range; sizing it for
    // white noise (as this first did) turned it into a +9 dB bass boost that
    // made darker settings louder.
    outGain_ *= powf(dampCoef_ / (2.f - dampCoef_), -0.08f);
    lowCoef_ = clampf(c.lowCut, 0.f, 1.f) * 0.0035f + 0.00002f;
    diffCoef_ = clampf(c.diffusion, 0.f, 0.85f);
    modDepth_ = c.modDepth < 0.f ? 0.f : c.modDepth;
    modInc_ = clampf(c.modRate, 0.f, 12.f) / kSampleRate;
    width_ = clampf(c.width, 0.f, 1.f);
    // L and R are uncorrelated, so narrowing throws away side energy:
    // per-channel power goes to (1+w^2)/2. Put it back so width is not a
    // hidden volume control.
    widthGain_ = 1.f / sqrtf((1.f + width_ * width_) * 0.5f);
    dispStages_ = c.dispStages < 0 ? 0 : (c.dispStages > 24 ? 24 : c.dispStages);
    dispCoef_ = clampf(c.dispCoef, 0.f, 0.93f);
    float ps = c.preDelay * kSampleRate;
    preSamp_ = preMax_ > 4 ? clampf(ps, 1.f, (float)(preMax_ - 4)) : 0.f;
    preInt_ = (uint32_t)preSamp_;
  }

  inline void process(float inL, float inR, float &outL, float &outR) {
    float x = (inL + inR) * 0.5f;
    if (preSamp_ > 0.f) {
      pre_.write(x);
      x = pre_.readInt(preInt_);  // a static pre-delay needs no interpolation
    }
    // Tail modulation runs at control rate: the LFOs are < 12 Hz and a few
    // samples deep, so a 16-sample step moves a tap by ~0.05 samples at most —
    // inaudible, and it removes 8 sine evaluations from every sample.
    if (modCount_ == 0) {
      modCount_ = 16;
      for (int i = 0; i < kLines; ++i) {
        modPh_[i] += modInc_ * modMul_[i] * 16.f;
        if (modPh_[i] >= 1.f) modPh_[i] -= 1.f;
        modOff_[i] = modDepth_ * fastSin01(modPh_[i]);
      }
    }
    --modCount_;
    // Input diffusion: smears the transient into a dense burst so the tank is
    // fed noise rather than a click. This is what stops the flutter echo.
    for (int i = 0; i < kDiffusers; ++i) x = diff_[i].process(x, diffCoef_);

    float v[kLines];
    for (int i = 0; i < kLines; ++i) {
      float d = delay_[i] + modOff_[i];
      if (d > maxLen_[i]) d = maxLen_[i];
      float s = line_[i].read(d);
      lpz_[i] += dampCoef_ * (s - lpz_[i]);          // HF absorption
      s = lpz_[i];
      hpz_[i] += lowCoef_ * (s - hpz_[i]);           // LF absorption
      s -= hpz_[i];
      v[i] = s * g_[i];
    }
    // Allpass sections are unity-gain, so dispersing inside the loop changes
    // the phase response without adding energy — the network stays stable at
    // any stage count. (An external feedback path around the tank does not:
    // its loop gain exceeds 1 at the modal peaks and self-oscillates.)
    if (dispStages_ > 0) {
      v[0] = disp_[0].process(v[0], dispCoef_, dispStages_);
      v[1] = disp_[1].process(v[1], dispCoef_, dispStages_);
    }

    // Taps for the stereo output are taken before the mix, so L and R see
    // different (uncorrelated) subsets of the network. Alternating signs stop
    // the lines summing coherently at low frequencies (short delays vs a long
    // period), which otherwise adds up to +6 dB of bass to the tail.
    float l = v[0] - v[2] + v[4] - v[6];
    float r = v[1] - v[3] + v[5] - v[7];

    hadamard8(v);  // orthogonal => energy-preserving => unconditionally stable

    const float inj = x * kInject_;
    for (int i = 0; i < kLines; ++i)
      line_[i].write(v[i] + ((i & 1) ? -inj : inj));

    l *= outGain_;
    r *= outGain_;
    const float mid = (l + r) * 0.5f;
    const float side = (l - r) * 0.5f * width_;
    outL = (mid + side) * widthGain_;
    outR = (mid - side) * widthGain_;
  }

  // --- legacy call signature -------------------------------------------
  // Maps the old (roomsize, damp, extraFeed) triple onto the new engine so
  // units that have not been ported yet keep working (and get the level fix).
  inline void process(float inL, float inR, float &outL, float &outR,
                      float roomsize, float damp, float extraFeed = 0.f) {
    if (roomsize != lRoom_ || damp != lDamp_ || extraFeed != lExtra_) {
      lRoom_ = roomsize;
      lDamp_ = damp;
      lExtra_ = extraFeed;
      Config c;
      c.size = 0.55f + roomsize * 0.95f;
      c.decay = 0.5f + roomsize * roomsize * 7.f + extraFeed * 55.f;
      c.damp = damp;
      c.diffusion = 0.72f;
      c.modDepth = 5.f;
      c.modRate = 0.6f;
      setConfig(c);
    }
    process(inL, inR, outL, outR);
  }

 private:
  // 8-point fast Walsh-Hadamard transform, scaled by 1/sqrt(8) to stay unitary.
  static inline void hadamard8(float *x) {
    for (int s = 1; s < kLines; s <<= 1)
      for (int i = 0; i < kLines; i += (s << 1))
        for (int j = i; j < i + s; ++j) {
          const float a = x[j], b = x[j + s];
          x[j] = a + b;
          x[j + s] = a - b;
        }
    for (int i = 0; i < kLines; ++i) x[i] *= 0.35355339f;
  }

  static constexpr float kInject_ = 0.35355339f;  // 1/sqrt(8)
  static constexpr float kNorm_ = 1.7f;           // wet ~ dry on pink noise

  DelayLine line_[kLines];
  Allpass diff_[kDiffusers];
  Disperser<24> disp_[2];
  DelayLine pre_;
  float delay_[kLines] = {0}, maxLen_[kLines] = {0}, g_[kLines] = {0};
  float lpz_[kLines] = {0}, hpz_[kLines] = {0};
  float modPh_[kLines] = {0}, modMul_[kLines] = {0}, modOff_[kLines] = {0};
  uint32_t modCount_ = 0, preInt_ = 0;
  float maxSize_ = 2.f, outGain_ = 1.f, dampCoef_ = 0.3f, lowCoef_ = 0.001f;
  float diffCoef_ = 0.72f, modDepth_ = 6.f, modInc_ = 0.f, width_ = 1.f;
  float dispCoef_ = 0.72f, widthGain_ = 1.f;
  int dispStages_ = 0;
  uint32_t preMax_ = 0;
  float preSamp_ = 0.f;
  float lRoom_ = -1.f, lDamp_ = -1.f, lExtra_ = -1.f;
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
    // phase_ is a delay behind the write head, which advances one sample per
    // sample. To read at speed `ratio` the delay must *shrink* by (ratio - 1)
    // per sample. (It used to grow, so ratio 2 froze the read head and 1.5
    // played an octave down — every "octave-up" shimmer was a buzz.)
    phase_ -= (ratio - 1.f);
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
    // The Chamberlin recursion is only stable while f^2 + 2fq < 4, i.e.
    // f < sqrt(q^2 + 4) - q. Above that (cutoff past ~SR/5 at low resonance)
    // it blows up to inf and the NaN then lives in every feedback path
    // downstream forever — which is how wahdelverb went permanently silent.
    const float fMax = 0.9f * (sqrtf(q * q + 4.f) - q);
    if (f > fMax) f = fMax;
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

// Bounded soft saturator: ~linear below 0.5, never exceeds +/-1. Rational tanh
// approximation, cheap enough to sit in every feedback path.
inline float softLimit(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

// ---------------------------------------------------------------------------
// StereoDelay — a performance delay.
//  * Times glide towards their target (tape-style pitch bend when X moves)
//    instead of jumping, so sweeping the pad never clicks.
//  * The feedback path has its own filters and a soft limiter, so feedback
//    above 100% turns into a controlled, darkening self-oscillation rather than
//    a blow-up. That is the "hard" delay: you can ride it into runaway.
//  * Optional ping-pong cross-feed, wow/flutter, reverse playback of the
//    repeats, and pitch-shifting inside the loop (each repeat higher/lower).
// process() returns the wet signal only.
// ---------------------------------------------------------------------------
class StereoDelay {
 public:
  struct Config {
    float timeL = 12000.f;   // samples
    float timeR = 12000.f;
    float feedback = 0.45f;  // 0..1.15 (>1 = self-oscillation, limited)
    float cross = 0.f;       // 0 = straight, 1 = full ping-pong
    float hiCut = 0.25f;     // 0..1 how much each repeat darkens
    float loCut = 0.1f;      // 0..1 how much each repeat thins out
    float drive = 0.f;       // 0..1 saturation in the loop (tape grit)
    float wow = 0.f;         // samples of slow (~0.7 Hz) time wobble
    float flutter = 0.f;     // samples of fast (~6.5 Hz) time wobble
    float pitch = 1.f;       // pitch ratio applied in the loop (needs withPitch)
    bool reverse = false;    // repeats play backwards, in chunks of `time`
    float glide = 0.0006f;   // per-sample slew of the delay time
  };

  bool init(BufferAllocator &a, float maxSeconds, bool withPitch) {
    const uint32_t n = (uint32_t)(maxSeconds * kSampleRate) + 8;
    float *bl = a.alloc(n), *br = a.alloc(n);
    if (!bl || !br) return false;
    line_[0].init(bl, n);
    line_[1].init(br, n);
    maxT_ = (float)(n - 8);
    hasPitch_ = false;
    if (withPitch) {
      float *pl = a.alloc(kPitchBuf), *pr = a.alloc(kPitchBuf);
      if (!pl || !pr) return false;
      ps_[0].init(pl, kPitchBuf);
      ps_[1].init(pr, kPitchBuf);
      hasPitch_ = true;
    }
    for (int c = 0; c < 2; ++c) { t_[c] = 12000.f; lp_[c] = hp_[c] = 0.f; revPh_[c] = 0.f; y_[c] = 0.f; }
    wowPh_ = 0.f; flPh_ = 0.37f;
    setConfig(Config());
    return true;
  }

  void setConfig(const Config &c) {
    c_ = c;
    const float lim = c.reverse ? maxT_ * 0.5f - 4.f : maxT_ - 64.f;
    c_.timeL = clampf(c.timeL, 16.f, lim);
    c_.timeR = clampf(c.timeR, 16.f, lim);
    c_.feedback = clampf(c.feedback, 0.f, 1.15f);
    c_.cross = clampf(c.cross, 0.f, 1.f);
    hc_ = expf(-clampf(c.hiCut, 0.f, 1.f) * 3.5f);          // LP coefficient
    lc_ = clampf(c.loCut, 0.f, 1.f) * 0.02f + 0.0005f;      // HP coefficient
    drv_ = 1.f + clampf(c.drive, 0.f, 1.f) * 1.5f;
    if (!hasPitch_) c_.pitch = 1.f;
  }

  inline void process(float inL, float inR, float &outL, float &outR) {
    wowPh_ += 0.7f / kSampleRate;  if (wowPh_ >= 1.f) wowPh_ -= 1.f;
    flPh_ += 6.5f / kSampleRate;   if (flPh_ >= 1.f) flPh_ -= 1.f;
    const float mod = c_.wow * fastSin01(wowPh_) + c_.flutter * fastSin01(flPh_);
    const float target[2] = {c_.timeL, c_.timeR};
    for (int c = 0; c < 2; ++c) {
      t_[c] += c_.glide * (target[c] - t_[c]);
      y_[c] = c_.reverse ? readReverse(c) : line_[c].read(clampf(t_[c] + mod, 2.f, maxT_));
    }
    // Ping-pong needs the input to enter one side only; cross-feeding a signal
    // that is already on both sides just gives two identical echoes.
    const float mono = (inL + inR) * 0.5f;
    const float in[2] = {lerp(inL, mono, c_.cross), inR * (1.f - c_.cross)};
    for (int c = 0; c < 2; ++c) {
      // ping-pong: each side is fed by the other side's repeats
      float fb = y_[c] * (1.f - c_.cross) + y_[1 - c] * c_.cross;
      lp_[c] += hc_ * (fb - lp_[c]);
      fb = lp_[c];
      hp_[c] += lc_ * (fb - hp_[c]);
      fb -= hp_[c];
      if (c_.pitch != 1.f) fb = ps_[c].process(fb, c_.pitch);
      // small-signal gain stays exactly 1 (drive never changes the feedback
      // amount); loud repeats are squashed towards 1/drv_ — tape compression.
      fb = softLimit(fb * drv_) / drv_;
      line_[c].write(softLimit(in[c] + fb * c_.feedback));
    }
    outL = y_[0];
    outR = y_[1];
  }

 private:
  // Two heads each play a `time`-long chunk backwards (read delay 2*ph grows
  // twice as fast as the write head advances => backwards at unit speed),
  // offset by half a chunk and Hann-crossfaded so the seams are inaudible.
  inline float readReverse(int c) {
    const float w = t_[c];
    revPh_[c] += 1.f;
    if (revPh_[c] >= w) revPh_[c] -= w;
    float p2 = revPh_[c] + w * 0.5f;
    if (p2 >= w) p2 -= w;
    const float e1 = 0.5f - 0.5f * fastSin01(frac(revPh_[c] / w + 0.25f));
    const float e2 = 0.5f - 0.5f * fastSin01(frac(p2 / w + 0.25f));
    return line_[c].read(2.f * revPh_[c] + 2.f) * e1 + line_[c].read(2.f * p2 + 2.f) * e2;
  }
  static inline float frac(float x) { return x - (float)(int)x; }

  static constexpr uint32_t kPitchBuf = 7208;  // 75 ms grains in the shifter
  DelayLine line_[2];
  PitchShifter ps_[2];
  Config c_;
  float t_[2] = {0, 0}, lp_[2] = {0, 0}, hp_[2] = {0, 0}, revPh_[2] = {0, 0}, y_[2] = {0, 0};
  float maxT_ = 1.f, hc_ = 0.5f, lc_ = 0.001f, drv_ = 1.f, wowPh_ = 0.f, flPh_ = 0.f;
  bool hasPitch_ = false;
};

// Musical note values, in beats, for tempo-synced times: X on the pad snaps to
// these so a delay always lands on the grid. 1/16 .. 1 bar incl. triplets/dotted.
static const float kNoteBeats[12] = {0.25f,  1.f / 3.f, 0.375f, 0.5f, 2.f / 3.f, 0.75f,
                                     1.f,    4.f / 3.f, 1.5f,   2.f,  3.f,       4.f};
static const char *const kNoteNames[12] = {"1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
                                           "1/4",  "1/2T", "1/4D",  "1/2", "1/2D", "1BAR"};
inline int noteIndex(float x01, int maxIndex = 11) {
  int i = (int)(clampf(x01, 0.f, 1.f) * (float)(maxIndex + 1));
  return i > maxIndex ? maxIndex : i;
}

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
//   0 -> clean, 0.25 -> warm (~1-2% THD on program material),
//   0.5 -> driven, 1.0 -> brutal fuzz with wavefold bite.
// Level-compensated around typical program level (-12 dBFS peaks), so DRIVE
// changes the character, not the volume. (The first version pushed ~6% THD
// and +9 dB at its 0.25 default — every unit's wet was being crunched and
// compressed into the same sound, and DRIVE doubled as a 14 dB volume knob.)
inline float grit(float x, float amount) {
  if (amount <= 0.0005f) return x;
  // Per-amount constants change once per knob move, not per sample.
  static float lastAmt = -1.f, pre = 1.f, makeup = 1.f, foldMix = 0.f, wet = 1.f;
  if (amount != lastAmt) {
    lastAmt = amount;
    wet = amount < 0.2f ? amount * 5.f : 1.f;  // fade the saturator in: no jump off 0
    pre = 1.f + 19.f * amount * amount * (0.6f + 0.4f * amount);  // 1 .. 20
    makeup = 0.25f / tanhf(pre * 0.25f);                          // unity at -12 dBFS
    foldMix = amount > 0.6f ? (amount - 0.6f) * 0.9f : 0.f;       // 0 .. 0.36
  }
  float y = tanhf(x * pre);
  if (foldMix > 0.f) y = lerp(y, wavefold(x * pre * 0.5f, 1.f), foldMix);
  y = lerp(x, y * makeup, wet);
  return y < -1.2f ? -1.2f : (y > 1.2f ? 1.2f : y);
}

// Apply grit to the wet signal, then dry/wet blend. Used by every unit's output.
inline float driveMix(float dry, float drive, float wet, float mix,
                      float wetTrim = 1.f) {
  return lerp(dry, grit(wet * wetTrim, drive), mix);
}

// Cheap xorshift RNG -> [0,1).
class Rng {
 public:
  void seed(uint32_t s) { s_ = s ? s : 0x1234abcdu; }
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
  static constexpr int kMaxGrains = 64;  // max simultaneous grains ("ripples")

  // Per-grain randomisation and envelope, set per block. The defaults
  // reproduce plain behaviour (no jitter, forward grains, Hann window), so
  // units that never call setTexture() are unaffected.
  struct Texture {
    float shape = 0.5f;        // envelope: 0 percussive .. 0.5 Hann .. 1 gate (flat top)
    float pitchJitter = 0.f;   // random per-grain pitch offset, +/- semitones (0..24)
    int pitchQuant = 0;        // 0 free (detuned) 1 semitones 2 octaves 3 octaves+fifths
    float reverseProb = 0.f;   // chance a grain plays backwards (0..1)
    float sizeJitter = 0.f;    // random per-grain length variation (0..1)
    float timeJitter = 0.f;    // irregular grain onsets: 0 steady stream .. 1 scattered
    float width = -1.f;        // stereo spread of grains, <0 = use `spread`
  };

  void init(float *buf, uint32_t size) {
    rec_.init(buf, size);
    timer_ = 0;
    held_ = false;
    valid_ = 0;
    env_ = 0.f;
    captureMin_ = 48000;
    span_ = size > 2 ? (float)(size - 2) : 1.f;
    rng_.seed(0x1234abcdu);
    tex_ = Texture();
    lastDensity_ = -1.f;
    norm_ = 0.5f;
    for (int i = 0; i < kMaxGrains; ++i) g_[i].active = false;
  }
  void setTexture(const Texture &t) { tex_ = t; }

  // --- capture / hold -------------------------------------------------------
  // `freeze` asks to hold the buffer, but the hold only engages once the buffer
  // actually contains sound: it keeps recording until `captureMin` samples of
  // signal have arrived since the last onset. While held, grain positions span
  // only that captured material. Without this, a unit that loads (or is
  // switched) into a freeze mode holds two seconds of silence forever.
  void recapture() { held_ = false; valid_ = 0; }  // grab fresh sound, then re-lock
  void setCaptureMin(uint32_t samples) { captureMin_ = samples < 480 ? 480 : samples; }
  bool held() const { return held_; }

  // density, grainSize, spread, position in [0,1]; pitch is a ratio (negative =
  // reverse); freeze asks to hold the buffer. grainSize maps exponentially from
  // 3 ms (buzzy microsound) through ~55 ms at 0.5 to 1 s (long smears).
  inline void process(float in, float density, float grainSize, float pitch,
                      float spread, float position, bool freeze, float &outL,
                      float &outR) {
    const uint32_t maxDelay = rec_.size() - 2;
    const float a = fabsf(in);
    env_ = a > env_ ? a : env_ * 0.99995833f;  // ~0.5 s release
    if (!freeze) held_ = false;
    if (!held_) {
      rec_.write(in);
      if (env_ > 0.001f) {                     // -60 dBFS
        if (valid_ < maxDelay) ++valid_;
      } else {
        valid_ = 0;  // silence: a later freeze should grab the next sound, not this
      }
      if (freeze && valid_ >= captureMin_) held_ = true;
    }
    span_ = held_ ? (float)(valid_ > 480 ? valid_ : 480) : (float)maxDelay;

    // grains per grain-length (average overlap); normalise so that density
    // changes the texture rather than the volume (uncorrelated grains add in
    // power: sqrt(overlap * mean Hann^2 = 0.375)).
    const float overlap = 0.6f + density * 14.f;
    if (density != lastDensity_) {
      lastDensity_ = density;
      const float e = 0.375f * overlap;
      norm_ = 0.85f / sqrtf(e > 1.f ? e : 1.f);
    }

    if (timer_ <= 0.f) {
      const float g = clampf(grainSize, 0.f, 1.f);
      const float dur = 144.f * exp2f(g * 8.38f);  // 3 ms .. 1 s (2^8.38 = 333)
      spawn(dur, pitch, spread, position);
      float interval = dur / overlap;
      if (tex_.timeJitter > 0.f) interval *= 1.f + tex_.timeJitter * (rng_.next() * 2.f - 1.f) * 0.9f;
      timer_ = interval < 1.f ? 1.f : interval;
    }
    timer_ -= 1.f;

    // envelope morph weights for this block's shape
    const float sh = clampf(tex_.shape, 0.f, 1.f);
    const float wPerc = sh < 0.5f ? 1.f - sh * 2.f : 0.f;
    const float wGate = sh > 0.5f ? (sh - 0.5f) * 2.f : 0.f;
    const float wHann = 1.f - wPerc - wGate;

    float l = 0.f, r = 0.f;
    for (int i = 0; i < kMaxGrains; ++i) {
      Grain &gr = g_[i];
      if (!gr.active) continue;
      const float x = gr.t * gr.invDur;  // 0..1 through the grain
      float env = 0.f;
      if (wHann > 0.f) env += wHann * (0.5f - 0.5f * fastSin01(wrap01(x + 0.25f)));
      if (wPerc > 0.f) {
        const float d = 1.f - x;
        env += wPerc * (x < 0.04f ? x * 25.f : d * d * 1.085f);
      }
      if (wGate > 0.f) {
        const float e = x < 0.1f ? x * 10.f : (x > 0.9f ? (1.f - x) * 10.f : 1.f);
        env += wGate * e * e * (3.f - 2.f * e);  // smoothstep fades
      }
      const float s = rec_.read(gr.pos) * env;
      l += s * gr.panL;
      r += s * gr.panR;
      // pos is a delay behind the write head. While recording, the head itself
      // moves forward one sample per sample, so the grain only has to move by
      // (inc - 1) to play at rate `inc`. (Moving by inc here played every live
      // grain at pitch + 1: unison came out an octave up, OCT- a fifth up.)
      gr.pos -= held_ ? gr.inc : gr.inc - 1.f;
      // wrap inside the playable span (the captured sound when held)
      if (gr.pos < 1.f) gr.pos += span_;
      if (gr.pos > span_) gr.pos = fmodf(gr.pos, span_) + 1.f;
      gr.t += 1.f;
      if (gr.t >= gr.dur) gr.active = false;
    }
    outL = l * norm_;
    outR = r * norm_;
  }

 private:
  struct Grain {
    bool active;
    float pos, t, dur, invDur, inc, panL, panR;
  };
  static inline float wrap01(float x) { return x >= 1.f ? x - 1.f : x; }

  // Random pitch offset in semitones, snapped to the chosen scale.
  inline float jitterSemis() {
    const float j = tex_.pitchJitter;
    if (j <= 0.f) return 0.f;
    float s = (rng_.next() * 2.f - 1.f) * j;
    switch (tex_.pitchQuant) {
      case 1: s = floorf(s + 0.5f); break;                            // semitones
      case 2: s = 12.f * floorf(s / 12.f + 0.5f); break;              // octaves
      case 3: {                                                       // octaves + fifths
        static const float kSteps[7] = {-24.f, -12.f, -5.f, 0.f, 7.f, 12.f, 19.f};
        float best = 0.f, bd = 99.f;
        for (int k = 0; k < 7; ++k) {
          const float d = fabsf(kSteps[k] - s);
          if (d < bd && fabsf(kSteps[k]) <= j + 0.5f) { bd = d; best = kSteps[k]; }
        }
        s = best;
        break;
      }
      default: break;
    }
    return s;
  }

  void spawn(float dur, float pitch, float spread, float position) {
    for (int i = 0; i < kMaxGrains; ++i) {
      if (g_[i].active) continue;
      if (tex_.sizeJitter > 0.f) dur *= 1.f + tex_.sizeJitter * (rng_.next() * 2.f - 1.f) * 0.75f;
      float inc = pitch;
      const float semis = jitterSemis();
      if (semis != 0.f) inc *= exp2f(semis * (1.f / 12.f));
      if (tex_.reverseProb > 0.f && rng_.next() < tex_.reverseProb) inc = -inc;
      // A live grain's delay moves by (inc - 1) per sample; a grain long enough
      // to travel past the whole buffer is shortened to fit.
      if (!held_) {
        const float speed = fabsf(inc - 1.f);
        if (speed * dur > span_ * 0.9f) dur = span_ * 0.9f / speed;
      }
      if (dur < 48.f) dur = 48.f;
      // Window of valid start delays. Live: the whole buffer minus margins,
      // narrowed so the grain's travel ((inc - 1) * dur) neither crosses the
      // write head (pitch up) nor runs off the oldest sample (pitch down /
      // reverse). Held: only the captured sound.
      const float margin = span_ < 9600.f ? span_ * 0.25f : 2400.f;
      float lo = margin, hi = span_ - margin;
      if (!held_) {
        const float travel = (inc - 1.f) * dur;
        if (travel > 0.f && lo < travel + 2.f) lo = travel + 2.f;
        if (travel < 0.f && hi > span_ + travel - 2.f) hi = span_ + travel - 2.f;
      }
      if (hi < lo) hi = lo;
      // Position + jitter are *mapped* into that window (reflecting at the
      // edges), never clamped: clamping piles every grain onto the same start
      // sample, and grains spawned at a steady rate then read a steady phase
      // progression that cancels — long reverse grains came out ~24 dB down.
      float u = position + (rng_.next() - 0.5f) * spread * 0.5f;
      if (u < 0.f) u = -u;
      if (u > 1.f) u = 2.f - u;
      float pos = lo + clampf(u, 0.f, 1.f) * (hi - lo);
      if (pos < 1.f) pos = 1.f;
      if (pos > span_) pos = span_;
      // equal-power pan across the full width
      const float w = tex_.width >= 0.f ? tex_.width : spread;
      const float p = (rng_.next() * 2.f - 1.f) * clampf(w, 0.f, 1.f);
      g_[i].active = true;
      g_[i].pos = pos;
      g_[i].t = 0.f;
      g_[i].dur = dur;
      g_[i].invDur = 1.f / dur;
      g_[i].inc = inc;
      g_[i].panL = sqrtf(1.f - p);
      g_[i].panR = sqrtf(1.f + p);
      return;
    }
  }

  DelayLine rec_;
  Grain g_[kMaxGrains];
  float timer_ = 0.f;
  Rng rng_;
  Texture tex_;
  bool held_ = false;
  uint32_t valid_ = 0, captureMin_ = 48000;  // 1 s of real sound before a hold locks
  float env_ = 0.f, span_ = 1.f, lastDensity_ = -1.f, norm_ = 0.5f;
};

// A grain texture as one table row, so units can keep per-mode textures in a
// compact static array: {shape, pitchJitter, pitchQuant, reverseProb,
// sizeJitter, timeJitter, width (<0 = use spread)}.
inline GrainCloud::Texture texture(const float *r) {
  GrainCloud::Texture t;
  t.shape = r[0];
  t.pitchJitter = r[1];
  t.pitchQuant = (int)r[2];
  t.reverseProb = r[3];
  t.sizeJitter = r[4];
  t.timeJitter = r[5];
  t.width = r[6];
  return t;
}

// The three spare parameter slots (5..7) that every grain unit exposes, so the
// engine's per-grain randomisation is reachable from the NTS-3 edit menu (and
// assignable to X/Y). Each knob pushes the unit's own per-mode texture further.
struct GrainKnobs {
  float shape = 0.5f, jitter = 0.f, reverse = 0.f;  // 0..1 each, 0.5 shape = unchanged
  bool set(uint8_t index, int32_t value) {
    const float v = (float)value * (1.f / 1023.f);
    switch (index) {
      case 5: shape = v; return true;
      case 6: jitter = v; return true;
      case 7: reverse = v; return true;
      default: return false;
    }
  }
  void reset() { shape = 0.5f; jitter = 0.f; reverse = 0.f; }
  GrainCloud::Texture apply(GrainCloud::Texture t) const {
    // SHAPE: 0.5 keeps the mode's envelope, below leans percussive, above gated
    t.shape = clampf(t.shape + (shape - 0.5f) * 2.f * (shape < 0.5f ? t.shape : 1.f - t.shape), 0.f, 1.f);
    // SCATTER: pitch, size and timing randomness together, up to +/- 1 octave
    t.pitchJitter += jitter * 12.f;
    t.sizeJitter = clampf(t.sizeJitter + jitter * 0.8f, 0.f, 1.f);
    t.timeJitter = clampf(t.timeJitter + jitter * 0.9f, 0.f, 1.f);
    t.reverseProb = clampf(t.reverseProb + reverse, 0.f, 1.f);
    return t;
  }
};

// Tempo-synced beat repeat. Records continuously; trigger() latches the most
// recent `len` samples and loops them. Short fades at the seam make each repeat
// land as a clean rhythmic hit instead of a click. Reads straight from the
// ring, so a latched loop stays valid while age + len < buffer size — process()
// re-latches on its own if a caller holds one longer than that.
class BeatRepeat {
 public:
  void init(float *buf, uint32_t size) {
    rec_.init(buf, size);
    len_ = 4800.f;
    ph_ = 0.f;
    age_ = 0;
    active_ = false;
  }
  // Latch the last `len` samples. Takes effect immediately.
  void trigger(float len) {
    const float mx = (float)(rec_.size() / 2);
    len_ = clampf(len, 64.f, mx);
    ph_ = 0.f;
    age_ = 0;
    active_ = true;
  }
  void release() { active_ = false; }
  bool active() const { return active_; }

  // rate: playback speed through the slice (negative = reverse).
  // gate: fraction of each repeat that sounds (1 = legato, 0.25 = choppy).
  inline float process(float in, float rate, float gate) {
    rec_.write(in);
    if (!active_) return in;
    ++age_;
    if ((float)age_ + len_ > (float)rec_.size() - 8.f) trigger(len_);
    const float fade = len_ * 0.25f < 240.f ? len_ * 0.25f : 240.f;  // <= 5 ms
    const float open = len_ * clampf(gate, 0.05f, 1.f);
    float w = 1.f;
    if (ph_ < fade) w = ph_ / fade;
    if (open - ph_ < fade) w = (open - ph_) / fade;
    if (w < 0.f) w = 0.f;
    // loop offset ph_ was recorded (len_ - ph_) samples before the trigger
    const float y = rec_.read((float)age_ + len_ - ph_) * w;
    ph_ += rate;
    if (ph_ >= len_) ph_ -= len_;
    if (ph_ < 0.f) ph_ += len_;
    return y;
  }

 private:
  DelayLine rec_;
  float len_ = 4800.f, ph_ = 0.f;
  uint32_t age_ = 0;
  bool active_ = false;
};

}  // namespace dsp
