// tools/probe — measures one unit's effect.h natively, the way the NTS-3 runs
// it (fresh init, parameters set, 64-frame stereo blocks at 48 kHz).
//
// Built once per unit by scripts/probe_units.sh (-I units/<name>). For each
// MODE value it prints one line:
//   unit mode level_db tail_s centroid_hz corr ns_per_sample flags | fingerprint...
// level_db   full-wet output vs dry input on pink noise, DRIVE=0 (clean level)
// tail_s     after the input stops, seconds until the output is 60 dB down
//            (capped at 20; a freeze that holds forever reports 20)
// centroid   spectral centroid of the wet signal on the music source
// corr       L/R correlation (1 = mono, 0 = wide, <0 = phasey)
// flags      SILENT (wet < -40 dB), NAN, or ok
// fingerprint  band energies + envelope shape, used by --similar
#include "processor.h"
#include "effect.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static Effect fx;
static std::vector<float> mem;
static const int kB = 64;

// ---- sources ---------------------------------------------------------------
static uint32_t rs;
static float wn() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs & 0xFFFFFF) / 8388608.f - 1.f; }
struct Pink {  // Paul Kellet's filter
  float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  float next() {
    float w = wn();
    b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
    b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
    b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
    float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
    return p * 0.11f;
  }
};
// Plucked chords changing every half second, with a noise transient on each hit.
static float music(long i) {
  static const float ch[4][3] = {{220, 277.2f, 329.6f}, {196, 246.9f, 293.7f},
                                 {174.6f, 220, 261.6f}, {164.8f, 207.7f, 246.9f}};
  const long hit = i / 12000, ph = i % 12000;
  const float *c = ch[(hit / 2) % 4];
  const float env = expf(-(float)ph / 3000.f), t = i / 48000.f;
  float s = 0.f;
  for (int k = 0; k < 3; ++k) s += sinf(6.2831853f * c[k] * t) + 0.3f * sinf(6.2831853f * 2 * c[k] * t);
  return env * (0.16f * s + (ph < 240 ? 0.35f * wn() : 0.f));
}

// ---- tiny FFT for the spectrum ----------------------------------------------
static void fft(std::vector<float> &re, std::vector<float> &im) {
  const size_t n = re.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const float a = -6.2831853f / (float)len;
    for (size_t i = 0; i < n; i += len)
      for (size_t k = 0; k < len / 2; ++k) {
        const float wr = cosf(a * k), wi = sinf(a * k);
        const float ur = re[i + k], ui = im[i + k];
        const float vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        const float vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
      }
  }
}

static const int kBands = 12;  // half-octave-ish, 60 Hz .. 16 kHz

struct Run { std::vector<float> inL, outL, outR; double ns = 0; bool nan = false; };

static Run run(int mode, int x, int y, int drive, float seconds, float srcSeconds, bool pink) {
  Run r;
  mem.assign(fx.getBufferSize(), 0.f);
  fx.init(mem.data());
  fx.setTempo(120.f);
  fx.setParameter(0, x); fx.setParameter(1, y); fx.setParameter(2, 1000);
  fx.setParameter(3, mode); fx.setParameter(4, drive);
  rs = 0x9e3779b9u;
  Pink pk;
  float ib[kB * 2], ob[kB * 2];
  const long n = (long)(seconds * 48000), ns = (long)(srcSeconds * 48000);
  for (long i = 0; i < n; i += kB) {
    for (int k = 0; k < kB; ++k) {
      float s = (i + k) < ns ? (pink ? pk.next() : music(i + k)) : 0.f;
      ib[2 * k] = ib[2 * k + 1] = s;
    }
    auto t0 = std::chrono::steady_clock::now();
    fx.process(ib, ob, kB);
    r.ns += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    for (int k = 0; k < kB; ++k) {
      if (!(ob[2 * k] == ob[2 * k]) || !(ob[2 * k + 1] == ob[2 * k + 1])) r.nan = true;
      r.inL.push_back(ib[2 * k]); r.outL.push_back(ob[2 * k]); r.outR.push_back(ob[2 * k + 1]);
    }
  }
  r.ns /= (double)n;
  return r;
}

static double rms(const std::vector<float> &v, long a, long b) {
  double s = 0; for (long i = a; i < b; ++i) s += (double)v[i] * v[i];
  return sqrt(s / (double)(b - a));
}

int main(int argc, char **argv) {
  const char *unit = argc > 1 ? argv[1] : "?";
  const int x = argc > 2 ? atoi(argv[2]) : 256, y = argc > 3 ? atoi(argv[3]) : 512;
  for (int m = 0; m < 4; ++m) {
    const char *mn = fx.getParameterStrValue(3, m);
    // 1) clean level on pink noise
    Run p = run(m, x, y, 0, 4.f, 4.f, true);
    const double lvl = 20 * log10(rms(p.outL, 48000, 192000) / rms(p.inL, 48000, 192000) + 1e-12);
    // 2) music: spectrum + envelope while playing, then the tail
    Run mu = run(m, x, y, 0, 24.f, 4.f, false);
    const long s0 = 48000, s1 = 192000;
    const double wet = rms(mu.outL, s0, s1);
    // tail: last time the 10 ms envelope is above peak-60dB, after the input stops
    double pk = 0; for (long i = s1 - 4800; i < s1; ++i) pk = fmax(pk, fabs(mu.outL[i]));
    long last = s1;
    for (long i = s1; i + 480 < (long)mu.outL.size(); i += 480)
      if (rms(mu.outL, i, i + 480) > pk * 0.001 * 0.707) last = i;
    const double tail = (last - s1) / 48000.0;
    // spectrum of the wet signal while playing
    double band[kBands] = {0}, num = 0, den = 0;
    const size_t N = 4096;
    for (long f = s0; f + (long)N <= s1; f += N) {
      std::vector<float> re(N), im(N, 0.f);
      for (size_t i = 0; i < N; ++i) re[i] = mu.outL[f + i] * (0.5f - 0.5f * cosf(6.2831853f * i / N));
      fft(re, im);
      for (size_t k = 1; k < N / 2; ++k) {
        const double hz = k * 48000.0 / N, pw = (double)re[k] * re[k] + (double)im[k] * im[k];
        num += hz * pw; den += pw;
        if (hz < 60) continue;
        int b = (int)(log2(hz / 60.0) * 1.5); if (b >= kBands) b = kBands - 1;
        band[b] += pw;
      }
    }
    const double centroid = den > 0 ? num / den : 0;
    double bsum = 0; for (double b : band) bsum += b;
    // stereo
    double ll = 0, rr = 0, lr = 0;
    for (long i = s0; i < s1; ++i) { ll += mu.outL[i] * mu.outL[i]; rr += mu.outR[i] * mu.outR[i]; lr += mu.outL[i] * mu.outR[i]; }
    const double corr = (ll > 0 && rr > 0) ? lr / sqrt(ll * rr) : 1;
    // envelope shape: how much the 20 ms level moves (plucky input -> smooth wash?)
    double em = 0, ev = 0; int ne = 0;
    for (long i = s0; i + 960 < s1; i += 960) { double e = rms(mu.outL, i, i + 960); em += e; ev += e * e; ++ne; }
    em /= ne; ev = ev / ne - em * em;
    const double flux = em > 0 ? sqrt(fmax(ev, 0)) / em : 0;

    const bool silent = 20 * log10(wet / rms(mu.inL, s0, s1) + 1e-12) < -40;
    printf("%s\t%s\t%.1f\t%.2f\t%.0f\t%.2f\t%.1f\t%s\t|", unit, mn ? mn : "?", lvl, tail, centroid, corr,
           p.ns, (p.nan || mu.nan) ? "NAN" : (silent ? "SILENT" : "ok"));
    for (double b : band) printf(" %.4f", bsum > 0 ? b / bsum : 0);
    printf(" %.3f %.3f %.3f\n", fmin(tail, 20.0) / 20.0, flux, corr);
  }
  return 0;
}
