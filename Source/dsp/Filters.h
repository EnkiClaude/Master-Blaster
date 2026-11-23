#pragma once
// Topology-preserving-transform state-variable filter (Andrew Simper / Cytomic,
// "Linear Trapezoidal Integrated SVF"). It stays stable and clean when its
// coefficients move, which is why we can automate EQ without zipper noise.
#include "Common.h"

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
} // namespace mb
