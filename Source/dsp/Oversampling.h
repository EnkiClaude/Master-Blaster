#pragma once
// Low-latency oversampling built from polyphase IIR half-band filters
// (two parallel allpass chains; elliptic design after Olli Niemitalo / Laurent de
// Soras "hiir"). The group delay is a handful of samples, against the tens to
// hundreds a linear-phase FIR needs, and the image rejection stays high.
#include "Common.h"
#include <array>

namespace mb
{
namespace halfband
{
// Designs `n` allpass coefficients for a half-band filter with the given normalised
// transition bandwidth (0 < tbw < 0.5).
inline std::vector<double> design (int n, double tbw)
{
    double k = std::tan ((1.0 - tbw * 2.0) * kPi / 4.0);
    k *= k;
    const double kksqrt = std::pow (1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e2 = e * e, e4 = e2 * e2;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = n * 2 + 1;

    std::vector<double> coefs ((size_t) n);
    for (int index = 0; index < n; ++index)
    {
        const double c = index + 1;
        double num = 0.0;
        {
            int i = 0, j = 1;
            double term;
            do
            {
                term = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * kPi / order) * j;
                num += term; j = -j; ++i;
            } while (std::abs (term) > 1e-100 && i < 100);
        }
        num *= std::pow (q, 0.25);
        double den = 0.0;
        {
            int i = 1, j = -1;
            double term;
            do
            {
                term = std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * kPi / order) * j;
                den += term; j = -j; ++i;
            } while (std::abs (term) > 1e-100 && i < 100);
        }
        den += 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[(size_t) index] = (1.0 - x) / (1.0 + x);
    }
    return coefs;
}

class Stage
{
public:
    void init (const std::vector<double>& c)
    {
        coef = c;
        if (coef.size() % 2 != 0) coef.push_back (0.0); // a zero coefficient is a plain one-sample delay
        x.assign (coef.size(), 0.0);
        y.assign (coef.size(), 0.0);
    }
    void reset() { std::fill (x.begin(), x.end(), 0.0); std::fill (y.begin(), y.end(), 0.0); }

    // Runs the two allpass paths: s0 through the even coefficients, s1 through the odd ones.
    inline void run (double& s0, double& s1) noexcept
    {
        const size_t n = coef.size(); // always even (enforced in init)
        for (size_t i = 0; i < n; i += 2)
        {
            const double t0 = (s0 - y[i]) * coef[i] + x[i];
            const double t1 = (s1 - y[i + 1]) * coef[i + 1] + x[i + 1];
            x[i] = s0; x[i + 1] = s1;
            y[i] = undenorm (t0); y[i + 1] = undenorm (t1);
            s0 = t0; s1 = t1;
        }
    }

private:
    std::vector<double> coef, x, y;
};

class Up2x
{
public:
    void init (const std::vector<double>& c) { s.init (c); }
    void reset() { s.reset(); }
    inline void process (double in, double& out0, double& out1) noexcept
    {
        double a = in, b = in;
        s.run (a, b);
        out0 = a; out1 = b;
    }

private:
    Stage s;
};

class Down2x
{
public:
    void init (const std::vector<double>& c) { s.init (c); }
    void reset() { s.reset(); }
    inline double process (double in0, double in1) noexcept
    {
        double a = in1, b = in0;
        s.run (a, b);
        return 0.5 * (a + b);
    }

private:
    Stage s;
};
} // namespace halfband

// 4x oversampler for a single channel: a steep first stage and a cheap second
// stage (the second only has to reject images above 1.5x the base rate).
class Oversampler4x
{
public:
    Oversampler4x()
    {
        const auto c1 = halfband::design (16, 0.0255); // steep: passband edge ~20.9 kHz at 44.1 kHz
        const auto c2 = halfband::design (4, 0.255);
        up1.init (c1); down1.init (c1);
        up2.init (c2); down2.init (c2);
    }
    void reset() { up1.reset(); up2.reset(); down1.reset(); down2.reset(); }

    // Upsamples x into four samples, applies fn to each, and downsamples the result.
    template <typename Fn>
    inline double process (double x, Fn&& fn) noexcept
    {
        double a, b;
        up1.process (x, a, b);
        double u[4];
        up2.process (a, u[0], u[1]);
        up2.process (b, u[2], u[3]);
        for (double& v : u) v = fn (v);
        const double d0 = down2.process (u[0], u[1]);
        const double d1 = down2.process (u[2], u[3]);
        return down1.process (d0, d1);
    }

private:
    halfband::Up2x up1, up2;
    halfband::Down2x down1, down2;
};

// Eight-times windowed-sinc interpolator for estimating true (inter-sample)
// peaks. ITU-R BS.1770-4 Annex 2 asks for at least 4x; 8x with 32-tap branches
// keeps the worst-case underestimate for content up to 0.45 fs under ~0.15 dB.
// It runs on the detector path only, so it never touches the audio.
template <int Over, int Taps, int BetaX10>
class TruePeakDetectorT
{
public:
    static constexpr int kOver = Over;
    static constexpr int kTaps = Taps;            // taps per polyphase branch
    static constexpr int kDelay = kTaps / 2;      // base-rate delay of the estimate

    TruePeakDetectorT()
    {
        const int len = kTaps * kOver + 1;
        const double centre = kTaps * kOver * 0.5;
        const double beta = BetaX10 * 0.1;
        auto bessel0 = [] (double v)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k) { term *= (v / (2.0 * k)) * (v / (2.0 * k)); sum += term; }
            return sum;
        };
        std::vector<double> h ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            const double t = (i - centre) / kOver;
            const double sinc = t == 0.0 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double r = (i - centre) / centre;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            h[(size_t) i] = sinc * w;
        }
        for (int p = 1; p < kOver; ++p)
            for (int k = 0; k < kTaps; ++k)
            {
                const int idx = kOver * k + p;
                phase[(size_t) p - 1][(size_t) k] = idx < len ? h[(size_t) idx] : 0.0;
            }
        reset();
    }

    void reset() { hist.fill (0.0); pos = 0; }

    // Pushes a sample and returns the peak magnitude of the window around
    // x[n - kDelay]: both neighbouring samples plus the interpolated points
    // between them.
    inline double process (double x) noexcept
    {
        hist[pos] = x;
        hist[pos + kTaps] = x; // mirrored so each read is one contiguous span
        const double* h = &hist[pos + 1]; // h[kTaps-1] is the newest sample
        // v[0] = x[n-D], v[kOver] = x[n-D+1], interpolated points in between
        std::array<double, (size_t) kOver + 1> v {};
        v[0] = h[kTaps - 1 - kDelay];
        v[(size_t) kOver] = h[kTaps - kDelay];
        for (int p = 0; p < kOver - 1; ++p)
        {
            double acc = 0.0;
            const auto& c = phase[(size_t) p];
            for (int k = 0; k < kTaps; ++k) acc += c[(size_t) k] * h[kTaps - 1 - k];
            v[(size_t) p + 1] = acc;
        }
        int best = 0;
        double peak = 0.0;
        for (int i = 0; i <= kOver; ++i)
            if (std::abs (v[(size_t) i]) > peak) { peak = std::abs (v[(size_t) i]); best = i; }
        // Parabolic refinement: the true maximum usually falls between grid points.
        if (best > 0 && best < kOver)
        {
            const double a = std::abs (v[(size_t) best - 1]), b = peak, c = std::abs (v[(size_t) best + 1]);
            const double den = a - 2.0 * b + c;
            if (den < 0.0)
            {
                const double d = 0.5 * (a - c) / den;
                peak = std::max (peak, b - 0.25 * (a - c) * d);
            }
        }
        if (++pos == (size_t) kTaps) pos = 0;
        return peak;
    }

private:
    std::array<std::array<double, (size_t) kTaps>, (size_t) kOver - 1> phase {};
    std::array<double, (size_t) kTaps * 2 + 1> hist {};
    size_t pos = 0;
};

using TruePeakDetector = TruePeakDetectorT<16, 48, 80>;
} // namespace mb
