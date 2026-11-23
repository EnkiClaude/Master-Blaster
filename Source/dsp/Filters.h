#pragma once
// Topology-preserving-transform state-variable filter (Andrew Simper / Cytomic,
// "Linear Trapezoidal Integrated SVF"). It stays stable and clean when its
// coefficients move, which is why we can automate EQ without zipper noise.
#include "Common.h"
#include <array>
#include <complex>

namespace mb
{
class Svf
{
public:
    enum class Type { lowpass, highpass, bell, lowShelf, highShelf, allpass };

    void setup (Type t, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        freq = std::clamp (freq, 1.0, sampleRate * 0.49);
        q = std::max (q, 0.025);
        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (kPi * freq / sampleRate);
        double k = 1.0 / q;

        switch (t)
        {
            case Type::lowpass:   m0 = 0.0; m1 = 0.0; m2 = 1.0; break;
            case Type::highpass:  m0 = 1.0; m1 = -k;  m2 = -1.0; break;
            case Type::allpass:   m0 = 1.0; m1 = -2.0 * k; m2 = 0.0; break;
            case Type::bell:
                k = 1.0 / (q * A);
                m0 = 1.0; m1 = k * (A * A - 1.0); m2 = 0.0;
                break;
            case Type::lowShelf:
                g /= std::sqrt (A);
                m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0;
                break;
            case Type::highShelf:
                g *= std::sqrt (A);
                m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A;
                break;
        }
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    double process (double v0) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm (2.0 * v1 - ic1);
        ic2 = undenorm (2.0 * v2 - ic2);
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

// 4th-order Linkwitz-Riley crossover built from two cascaded Butterworth SVFs.
// low + high sums to a 2nd-order allpass: the bands recombine with a flat
// magnitude response and no latency.
class LR4Crossover
{
public:
    void setup (double freq, double sampleRate) noexcept
    {
        constexpr double q = 0.70710678118654752;
        for (auto* f : { &lp1, &lp2 }) f->setup (Svf::Type::lowpass, freq, q, 0.0, sampleRate);
        for (auto* f : { &hp1, &hp2 }) f->setup (Svf::Type::highpass, freq, q, 0.0, sampleRate);
    }
    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void process (double x, double& low, double& high) noexcept
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }

private:
    Svf lp1, lp2, hp1, hp2;
};

// The allpass that matches an LR4 crossover's phase. It keeps a band that
// skipped a crossover in phase with the bands that went through it.
class LR4Allpass
{
public:
    void setup (double freq, double sampleRate) noexcept
    {
        ap.setup (Svf::Type::allpass, freq, 0.70710678118654752, 0.0, sampleRate);
    }
    void reset() noexcept { ap.reset(); }
    double process (double x) noexcept { return ap.process (x); }

private:
    Svf ap;
};

// Gentle DC blocker (one-pole/one-zero high-pass).
class DcBlocker
{
public:
    void setup (double freq, double sampleRate) noexcept { r = std::exp (-2.0 * kPi * freq / sampleRate); }
    void reset() noexcept { x1 = y1 = 0.0; }
    double process (double x) noexcept
    {
        const double y = x - x1 + r * y1;
        x1 = x;
        y1 = undenorm (y);
        return y;
    }

private:
    double r = 0.9999, x1 = 0.0, y1 = 0.0;
};

// Second-order section whose magnitude response matches its analog prototype
// all the way to Nyquist (M. Vicanek, "Matched Second Order Digital Filters",
// 2016). A bilinear-transform EQ squeezes bands and shelves near Nyquist (the
// "cramping" that bends an air shelf out of shape at 44.1/48 kHz); this design
// does not:
//   poles come from impulse invariance,
//   the numerator is solved so |H| matches the analog |H| at DC, at Nyquist
//   and at the band's centre frequency.
// Runs as a transposed direct form II in double precision.
class MatchedBiquad
{
public:
    enum class Type { bell, lowShelf, highShelf };

    void setup (Type t, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        freq = std::clamp (freq, 5.0, sampleRate * 0.495);
        q = std::max (q, 0.05);
        if (std::abs (gainDb) < 1.0e-6) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        const double A = std::pow (10.0, gainDb / 40.0);
        const double sA = std::sqrt (A);

        // Analog prototype with s normalised to the corner, N(s)/D(s) (RBJ cookbook forms).
        double n2, n1, n0, d2, d1, d0;
        switch (t)
        {
            case Type::bell:      n2 = 1; n1 = A / q;  n0 = 1; d2 = 1; d1 = 1 / (A * q); d0 = 1; break;
            case Type::lowShelf:  n2 = A; n1 = A * sA / q; n0 = A * A; d2 = A; d1 = sA / q; d0 = 1; break;
            case Type::highShelf: default:
                                  n2 = A * A; n1 = A * sA / q; n0 = A; d2 = 1; d1 = sA / q; d0 = A; break;
        }
        const double w0 = 2.0 * kPi * freq / sampleRate;
        auto analogMag2 = [&] (double w) // w in rad/sample
        {
            const std::complex<double> s (0.0, w / w0);
            const auto h = (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
            return std::norm (h);
        };

        // Poles via impulse invariance of the analog denominator.
        const double wp = w0 * std::sqrt (d0 / d2);
        const double zeta = d1 / (2.0 * std::sqrt (d0 * d2));
        if (zeta <= 1.0) a1 = -2.0 * std::exp (-zeta * wp) * std::cos (wp * std::sqrt (1.0 - zeta * zeta));
        else             a1 = -2.0 * std::exp (-zeta * wp) * std::cosh (wp * std::sqrt (zeta * zeta - 1.0));
        a2 = std::exp (-2.0 * zeta * wp);

        const double A0 = (1 + a1 + a2) * (1 + a1 + a2);
        const double A1 = (1 - a1 + a2) * (1 - a1 + a2);
        const double A2 = -4.0 * a2;
        auto phis = [] (double w, double& p0, double& p1, double& p2)
        {
            p1 = std::sin (w * 0.5); p1 *= p1;
            p0 = 1.0 - p1;
            p2 = 4.0 * p0 * p1;
        };
        const double B0 = A0 * analogMag2 (0.0);
        const double B1 = A1 * analogMag2 (kPi);
        const double wm = std::min (w0, kPi * 0.9);
        double p0, p1, p2;
        phis (wm, p0, p1, p2);
        double B2 = (analogMag2 (wm) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        const double sB0 = std::sqrt (B0), sB1 = std::sqrt (B1);
        const double W = 0.5 * (sB0 + sB1);
        B2 = std::max (B2, -W * W); // keep the numerator realisable
        b0 = 0.5 * (W + std::sqrt (W * W + B2));
        b1 = 0.5 * (sB0 - sB1);
        b2 = -B2 / (4.0 * b0);
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = undenorm (b1 * x - a1 * y + z2);
        z2 = undenorm (b2 * x - a2 * y);
        return y;
    }

    // Magnitude of the digital filter in dB at frequency f (used by tests and the UI curve).
    double magnitudeDb (double f, double sampleRate) const noexcept
    {
        const std::complex<double> z = std::polar (1.0, -2.0 * kPi * f / sampleRate);
        const auto h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
        return 20.0 * std::log10 (std::abs (h));
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// Dynamic EQ band: a constant-Q bell whose gain follows the level inside its
// own band. It is built as x + (g - 1) * BP(x), where BP is a unity-peak SVF
// band-pass, so the gain can change every sample without recomputing any
// coefficients (no zipper noise, no latency). The same band-pass output feeds
// the detector, so the band reacts only to the frequencies it treats.
class DynamicBand
{
public:
    void setup (double freq, double q, double sampleRate) noexcept
    {
        const double g = std::tan (kPi * std::clamp (freq, 10.0, sampleRate * 0.45) / sampleRate);
        k = 1.0 / std::max (q, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { for (auto& s : st) s = {}; }

    // Band-pass (unity peak) of channel ch; updates the filter state.
    inline double bandpass (int ch, double v0) noexcept
    {
        auto& s = st[(size_t) ch];
        const double v3 = v0 - s.ic2;
        const double v1 = a1 * s.ic1 + a2 * v3;
        const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = undenorm (2.0 * v1 - s.ic1);
        s.ic2 = undenorm (2.0 * v2 - s.ic2);
        return k * v1;
    }

private:
    struct State { double ic1 = 0.0, ic2 = 0.0; };
    std::array<State, 2> st {};
    double k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
};
} // namespace mb
