#pragma once
// ITU-R BS.1770-4 / EBU R128 loudness meter: momentary (400 ms), short-term (3 s)
// and gated integrated loudness. The K-weighting coefficients are derived for
// any sample rate, using the analog prototype from libebur128.
#include "Common.h"
#include <array>

namespace mb
{
class Biquad
{
public:
    void set (double b0_, double b1_, double b2_, double a1_, double a2_) { b0 = b0_; b1 = b1_; b2 = b2_; a1 = a1_; a2 = a2_; }
    void reset() { z1 = z2 = 0.0; }
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = undenorm (b1 * x - a1 * y + z2);
        z2 = undenorm (b2 * x - a2 * y);
        return y;
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

class LoudnessMeter
{
public:
    static constexpr int kBins = 1000;           // histogram for the integrated gate
    static constexpr double kMinLufs = -70.0, kBinWidth = 0.1;

    void prepare (double sr)
    {
        sampleRate = sr;
        for (int ch = 0; ch < 2; ++ch)
        {
            double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            double K = std::tan (kPi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            double a0 = 1.0 + K / Q + K * K;
            pre[ch].set ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                         2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
            f0 = 38.13547087602444; Q = 0.5003270373238773;
            K = std::tan (kPi * f0 / sr);
            a0 = 1.0 + K / Q + K * K;
            rlb[ch].set (1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        hop = std::max (1, (int) std::round (sr * 0.1)); // 100 ms sub-blocks
        resetAll();
    }

    void resetAll()
    {
        for (int ch = 0; ch < 2; ++ch) { pre[ch].reset(); rlb[ch].reset(); }
        sub.fill (0.0);
        subPos = 0; subCount = 0; acc = 0.0; accN = 0;
        resetIntegrated();
        momentary = shortTerm = -150.0;
    }

    void resetIntegrated()
    {
        hist.fill (0); histE.fill (0.0);
        stHist.fill (0); stHistE.fill (0.0);
        integrated = -150.0;
        lra = 0.0;
        totalBlocks = 0;
    }

    inline void process (double l, double r) noexcept
    {
        const double kl = rlb[0].process (pre[0].process (l));
        const double kr = rlb[1].process (pre[1].process (r));
        acc += kl * kl + kr * kr; // channel weights G = 1.0 for L/R
        if (++accN >= hop) pushSubBlock();
    }

    double getMomentary() const noexcept { return momentary; }
    double getShortTerm() const noexcept { return shortTerm; }
    double getIntegrated() const noexcept { return integrated; }
    double getLoudnessRange() const noexcept { return lra; }
    long long getSubBlockCount() const noexcept { return subBlocksTotal; }

    static double energyToLufs (double e) { return e > 0.0 ? -0.691 + 10.0 * std::log10 (e) : -150.0; }

private:
    void pushSubBlock() noexcept
    {
        sub[(size_t) subPos] = acc / accN;
        subPos = (subPos + 1) % 30;
        subCount = std::min (subCount + 1, 30);
        acc = 0.0; accN = 0;

        auto meanOf = [this] (int n)
        {
            double s = 0.0;
            for (int i = 1; i <= n; ++i) s += sub[(size_t) ((subPos - i + 30) % 30)];
            return s / n;
        };
        if (subCount >= 4)
        {
            const double e = meanOf (4);
            momentary = energyToLufs (e);
            addGatingBlock (e, momentary);
        }
        if (subCount >= 30)
        {
            const double e = meanOf (30);
            shortTerm = energyToLufs (e);
            addShortTermBlock (e, shortTerm);
        }
        else if (subCount >= 4) shortTerm = energyToLufs (meanOf (subCount));
        ++subBlocksTotal;
    }

    // 400 ms gating blocks with 75 % overlap; the absolute gate is -70 LUFS and the relative gate -10 LU.
    void addGatingBlock (double e, double lufs) noexcept
    {
        if (lufs < kMinLufs) return;
        const int bin = std::clamp ((int) ((lufs - kMinLufs) / kBinWidth), 0, kBins - 1);
        ++hist[(size_t) bin]; histE[(size_t) bin] += e;
        ++totalBlocks;

        double sumE = 0.0; long long n = 0;
        for (int i = 0; i < kBins; ++i) { sumE += histE[(size_t) i]; n += hist[(size_t) i]; }
        if (n == 0) return;
        const double relGate = energyToLufs (sumE / (double) n) - 10.0;
        const int startBin = std::clamp ((int) std::ceil ((relGate - kMinLufs) / kBinWidth), 0, kBins);
        sumE = 0.0; n = 0;
        for (int i = startBin; i < kBins; ++i) { sumE += histE[(size_t) i]; n += hist[(size_t) i]; }
        integrated = n > 0 ? energyToLufs (sumE / (double) n) : -150.0;
    }

    // Loudness Range (EBU Tech 3342): the 3 s short-term values (every 100 ms),
    // gated at -70 LUFS absolute and 20 LU below their power average. LRA is
    // the spread between the 10th and 95th percentiles.
    void addShortTermBlock (double e, double lufs) noexcept
    {
        if (lufs < kMinLufs) return;
        const int bin = std::clamp ((int) ((lufs - kMinLufs) / kBinWidth), 0, kBins - 1);
        ++stHist[(size_t) bin]; stHistE[(size_t) bin] += e;

        double sumE = 0.0; long long n = 0;
        for (int i = 0; i < kBins; ++i) { sumE += stHistE[(size_t) i]; n += stHist[(size_t) i]; }
        if (n == 0) return;
        const double relGate = energyToLufs (sumE / (double) n) - 20.0;
        const int startBin = std::clamp ((int) std::ceil ((relGate - kMinLufs) / kBinWidth), 0, kBins);
        long long gated = 0;
        for (int i = startBin; i < kBins; ++i) gated += stHist[(size_t) i];
        if (gated < 2) { lra = 0.0; return; }
        auto percentile = [&] (double q)
        {
            const double target = q * (double) (gated - 1);
            long long cum = 0;
            for (int i = startBin; i < kBins; ++i)
            {
                cum += stHist[(size_t) i];
                if ((double) cum > target) return kMinLufs + (i + 0.5) * kBinWidth;
            }
            return kMinLufs + (kBins - 0.5) * kBinWidth;
        };
        lra = percentile (0.95) - percentile (0.10);
    }

    double sampleRate = 48000.0;
    Biquad pre[2], rlb[2];
    std::array<long long, kBins> stHist {};
    std::array<double, kBins> stHistE {};
    double lra = 0.0;
    long long subBlocksTotal = 0;
    std::array<double, 30> sub {};
    int subPos = 0, subCount = 0, hop = 4800, accN = 0;
    double acc = 0.0;
    std::array<long long, kBins> hist {};
    std::array<double, kBins> histE {};
    long long totalBlocks = 0;
    double momentary = -150.0, shortTerm = -150.0, integrated = -150.0;
};

// TPDF dither with optional first-order noise shaping, applied as the final
// stage when the mix is rendered to a fixed-point format.
class Dither
{
public:
    enum Mode { off = 0, tpdf24, tpdf16, shaped16 };

    void setMode (int m) noexcept
    {
        mode = m;
        const int bits = (m == tpdf24) ? 24 : 16;
        lsb = 1.0 / (double) (1 << (bits - 1));
    }
    void reset() { err[0] = err[1] = 0.0; }

    inline double process (double x, int ch) noexcept
    {
        if (mode == off) return x;
        const double tpdf = (rand01() - rand01()) * lsb;
        double v = x;
        if (mode == shaped16) v -= err[ch];
        const double q = std::round ((v + tpdf) / lsb) * lsb;
        if (mode == shaped16) err[ch] = q - v;
        return q;
    }

private:
    inline double rand01() noexcept
    {
        // xorshift64*: fast, good enough for dither and free of allocations
        state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
        return (double) ((state * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    }
    int mode = off;
    double lsb = 1.0 / 32768.0;
    double err[2] {};
    unsigned long long state = 0x9E3779B97F4A7C15ULL;
};
} // namespace mb
