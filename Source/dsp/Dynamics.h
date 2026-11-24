#pragma once
#include "Common.h"
#include "Oversampling.h"

namespace mb
{
// Feed-forward log-domain compressor: a soft-knee gain computer followed by a
// smooth branching peak detector on the gain-reduction signal (Giannoulis,
// Massberg & Reiss, "Digital Dynamic Range Compressor Design", JAES 2012).
// It gives clean attack and release with no release-time drift and none of the
// pumping a level detector placed before the gain computer causes.
class Compressor
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { yL = yF = yS = 0.0; rmsState = 0.0; }

    void setParams (double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs,
                    double rmsMs = 0.0, bool programDependentRelease = false) noexcept
    {
        thr = thresholdDb;
        slope = 1.0 - 1.0 / std::max (1.0, ratio);
        knee = std::max (0.0, kneeDb);
        aA = msToCoef (attackMs, sampleRate);
        aR = msToCoef (releaseMs, sampleRate);
        aRms = msToCoef (rmsMs, sampleRate);
        autoRel = programDependentRelease;
        // Program-dependent ("auto") release uses two envelopes:
        //   fast: normal attack, release at 1/3 of the setting, so transients recover quickly;
        //   slow: 15x slower attack and 4x slower release, so it tracks the sustained level.
        // The gain reduction is the larger of the two. A drum hit dips briefly and
        // comes straight back, while a dense passage settles into steady reduction
        // instead of pumping.
        aRFast = msToCoef (releaseMs / 3.0, sampleRate);
        aASlow = msToCoef (attackMs * 15.0 + 50.0, sampleRate);
        aRSlow = msToCoef (releaseMs * 4.0, sampleRate);
    }

    // Static curve: gain reduction in dB (>= 0) for an input level in dB.
    double staticReduction (double xdb) const noexcept
    {
        const double over = xdb - thr;
        if (knee > 0.0 && 2.0 * std::abs (over) <= knee)
        {
            const double t = over + knee * 0.5;
            return slope * t * t / (2.0 * knee);
        }
        return over > 0.0 ? slope * over : 0.0;
    }

    // Takes the linear detector input (the stereo-linked peak) and returns the linear gain.
    inline double process (double detector) noexcept
    {
        double level = std::abs (detector);
        if (aRms > 0.0)
        {
            rmsState = undenorm (level * level + (rmsState - level * level) * aRms);
            level = std::sqrt (rmsState * 2.0); // a sine's RMS reads as its peak, so thresholds stay comparable
        }
        const double xL = staticReduction (gainToDb (level, -200.0));
        if (! autoRel)
        {
            yL = xL > yL ? aA * yL + (1.0 - aA) * xL : aR * yL + (1.0 - aR) * xL;
            yL = undenorm (yL);
        }
        else
        {
            yF = xL > yF ? aA * yF + (1.0 - aA) * xL : aRFast * yF + (1.0 - aRFast) * xL;
            yS = xL > yS ? aASlow * yS + (1.0 - aASlow) * xL : aRSlow * yS + (1.0 - aRSlow) * xL;
            yF = undenorm (yF); yS = undenorm (yS);
            yL = std::max (yF, yS);
        }
        lastGain = std::exp (-yL * 0.11512925464970229); // dB -> linear
        return lastGain;
    }

    double getReductionDb() const noexcept { return yL; }
    double getLastGain() const noexcept { return lastGain; }

private:
    double sampleRate = 44100.0;
    double lastGain = 1.0;
    double thr = 0.0, slope = 0.0, knee = 6.0;
    double aA = 0.0, aR = 0.0, aRms = 0.0, aRFast = 0.0, aASlow = 0.0, aRSlow = 0.0;
    bool autoRel = false;
    double yL = 0.0, yF = 0.0, yS = 0.0, rmsState = 0.0;
};

// Monotonic-deque sliding-window minimum (O(1) amortised per sample).
class SlidingMin
{
public:
    void prepare (int maxLen)
    {
        cap = (size_t) maxLen + 2;
        vals.assign (cap, 1.0);
        idx.assign (cap, 0);
        setLength (maxLen);
    }
    void setLength (int len) { length = (long long) std::max (1, len); reset(); }
    void reset() { head = tail = 0; count = 0; n = 0; }

    inline double process (double v) noexcept
    {
        // Remove entries that can no longer be the minimum.
        while (count > 0 && vals[back()] >= v) { tail = (tail + cap - 1) % cap; --count; }
        vals[tail] = v; idx[tail] = n; tail = (tail + 1) % cap; ++count;
        // Remove the entry that has left the window.
        while (idx[head] <= n - length) { head = (head + 1) % cap; --count; }
        ++n;
        return vals[head];
    }

private:
    size_t back() const noexcept { return (tail + cap - 1) % cap; }
    std::vector<double> vals;
    std::vector<long long> idx;
    size_t cap = 0, head = 0, tail = 0, count = 0;
    long long length = 1, n = 0;
};

// Moving average over a ring buffer. The running sum is rebuilt exactly every
// `length` samples, so floating-point drift cannot build up.
class BoxFilter
{
public:
    void prepare (int maxLen) { buf.assign ((size_t) maxLen + 1, 1.0); setLength (maxLen); }
    void setLength (int len) { length = std::clamp (len, 1, (int) buf.size()); reset(); }
    void reset() { std::fill (buf.begin(), buf.end(), 1.0); sum = (double) length; pos = 0; }

    inline double process (double v) noexcept
    {
        sum += v - buf[(size_t) pos];
        buf[(size_t) pos] = v;
        if (++pos >= length)
        {
            pos = 0;
            double s = 0.0;
            for (int i = 0; i < length; ++i) s += buf[(size_t) i];
            sum = s;
        }
        return sum / length;
    }

private:
    std::vector<double> buf;
    double sum = 1.0;
    int length = 1, pos = 0;
};

// Transparent lookahead true-peak brickwall limiter (stereo-linked).
//
// For each sample, the detector computes the gain needed to keep the
// interpolated true peak under the ceiling. The gain then goes through:
//   instant attack with a program-dependent release (one-pole),
//   a sliding minimum over the lookahead window (hold), and
//   two cascaded moving averages whose lengths add up to the window + 1.
// The boxes give a smooth S-shaped attack ramp. Holding over the same window
// they average means the gain has reached its target by the time the delayed
// audio sample arrives, so the limiter does not overshoot (see the Signalsmith
// Audio limiter write-up for the same construction).
class TruePeakLimiter
{
public:
    void prepare (double sr, double maxLookaheadMs)
    {
        sampleRate = sr;
        maxLen = std::max (2, (int) std::ceil (maxLookaheadMs * 0.001 * sr) + 1);
        hold.prepare (maxLen);
        box1.prepare (maxLen);
        box2.prepare (maxLen);
        for (auto& d : delay) d.prepare (maxLen + TruePeakDetector::kDelay + 2);
        slowCoef = msToCoef (400.0, sr);
        setLookahead (maxLookaheadMs);
    }

    void setLookahead (double ms)
    {
        lookLen = std::clamp ((int) std::round (ms * 0.001 * sampleRate), 2, maxLen - 1);
        hold.setLength (lookLen);
        // B1 + B2 = L + 1
        const int b1 = (lookLen + 1) / 2;
        const int b2 = lookLen + 1 - b1;
        box1.setLength (b1);
        box2.setLength (b2);
        for (auto& d : delay) d.setDelay (getLatency());
        reset();
    }

    int getLatency() const noexcept { return TruePeakDetector::kDelay + lookLen - 1; }

    void setParams (double ceilingDb, double releaseMs, bool autoRelease, bool truePeak) noexcept
    {
        ceiling = dbToGain (ceilingDb);
        // The detectors only need to resolve peaks that could reach the ceiling (after the +0.06 dB margin).
        for (auto& t : tp) t.setSkipBelow (ceiling / 1.007 * 0.999);
        relCoefBase = releaseMs;
        autoRel = autoRelease;
        tpOn = truePeak;
    }

    void reset()
    {
        env = 1.0; slowGr = 0.0;
        hold.reset(); box1.reset(); box2.reset();
        for (auto& d : delay) d.reset();
        for (auto& t : tp) t.reset();
        grDb = 0.0;
    }

    inline void process (double& l, double& r) noexcept
    {
        // Peak detection (the true-peak estimate lags the input by kDelay samples).
        const double pl = tp[0].process (l), pr = tp[1].process (r);
        double peak;
        if (tpOn) peak = std::max (pl, pr) * 1.007; // +0.06 dB covers the detector's residual error (< 0.05 dB to 0.45 fs)
        else
        {
            // Sample-peak mode: use the same alignment as the true-peak path.
            const double sl = std::abs (tpDelay[0].process (l)), sr = std::abs (tpDelay[1].process (r));
            peak = std::max (sl, sr);
        }

        const double required = peak > ceiling ? ceiling / peak : 1.0;

        // Release: attack is instant here, because the hold and boxes shape it.
        if (required < env) env = required;
        else
        {
            double relMs = relCoefBase;
            if (autoRel)
            {
                // Program-dependent: isolated transients recover fast, while sustained
                // reduction (dense material) recovers slower so the limiter neither pumps nor distorts.
                relMs *= 1.0 + std::min (slowGr, 6.0) * 1.5;
            }
            const double c = msToCoef (relMs, sampleRate);
            env = required + (env - required) * c;
        }
        const double envDb = -gainToDb (env, -100.0);
        slowGr = envDb + (slowGr - envDb) * slowCoef;

        const double held = hold.process (env);
        const double g = box2.process (box1.process (held));

        const double dl = delay[0].process (l), dr = delay[1].process (r);
        l = dl * g;
        r = dr * g;
        grDb = -gainToDb (g, -100.0);
    }

    double getReductionDb() const noexcept { return grDb; }

private:
    double sampleRate = 44100.0;
    int maxLen = 2, lookLen = 2;
    SlidingMin hold;
    BoxFilter box1, box2;
    DelayLine delay[2];
    struct TpDelay
    {
        DelayLine d;
        TpDelay() { d.prepare (TruePeakDetector::kDelay + 1); d.setDelay (TruePeakDetector::kDelay); }
        double process (double x) noexcept { return d.process (x); }
    } tpDelay[2];
    TruePeakDetector tp[2];
    double ceiling = 1.0, relCoefBase = 100.0;
    bool autoRel = true, tpOn = true;
    double env = 1.0, slowGr = 0.0, slowCoef = 0.999, grDb = 0.0;
};
} // namespace mb
