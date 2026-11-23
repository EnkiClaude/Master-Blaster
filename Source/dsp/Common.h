#pragma once
// Master Blaster — shared DSP helpers (no framework dependencies).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace mb
{
constexpr double kPi = 3.14159265358979323846;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g, double floorDb = -150.0) noexcept
{
    return g > 0.0 ? std::max (floorDb, 20.0 * std::log10 (g)) : floorDb;
}

// Time constant -> one-pole coefficient (reach 1 - 1/e of a step in `ms`).
inline double msToCoef (double ms, double sampleRate) noexcept
{
    if (ms <= 0.0) return 0.0;
    return std::exp (-1.0 / (0.001 * ms * sampleRate));
}

// Flush denormals for feedback state without relying on FPU flags.
inline double undenorm (double x) noexcept { return std::abs (x) < 1.0e-30 ? 0.0 : x; }

// One-pole parameter smoother (exponential, ~`ms` time constant).
class Smoother
{
public:
    void prepare (double sampleRate, double ms) { coef = msToCoef (ms, sampleRate); }
    void reset (double v) noexcept { current = target = v; }
    void setTarget (double v) noexcept { target = v; }
    double next() noexcept { current = target + (current - target) * coef; return current; }
    // Advance n samples at once (used for per-chunk coefficient updates).
    double advance (int n) noexcept
    {
        current = target + (current - target) * std::pow (coef, (double) n);
        if (std::abs (current - target) < 1.0e-9) current = target;
        return current;
    }
    double value() const noexcept { return current; }
    double getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return current != target; }

private:
    double coef = 0.0, current = 0.0, target = 0.0;
};

// Integer delay line (single channel).
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        buf.assign ((size_t) std::max (1, maxDelay + 1), 0.0);
        pos = 0;
    }
    void setDelay (int d) noexcept { delay = std::clamp (d, 0, (int) buf.size() - 1); }
    int getDelay() const noexcept { return delay; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    double process (double x) noexcept
    {
        if (delay == 0) return x;
        const size_t n = buf.size();
        buf[pos] = x;
        size_t r = pos + n - (size_t) delay;
        if (r >= n) r -= n;
        const double y = buf[r];
        if (++pos == n) pos = 0;
        return y;
    }

private:
    std::vector<double> buf;
    size_t pos = 0;
    int delay = 0;
};
} // namespace mb
