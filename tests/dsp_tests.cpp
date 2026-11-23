// Numerical tests for the Master Blaster DSP core. They don't need JUCE.
#include "../Source/dsp/MasterChain.h"
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

using namespace mb;

static int failures = 0;
static void check (bool ok, const std::string& what, double value)
{
    std::printf ("[%s] %-60s %.6g\n", ok ? " OK " : "FAIL", what.c_str(), value);
    if (! ok) ++failures;
}

// Amplitude and phase of a long sine through `fn`, measured by correlation.
template <typename Fn>
static double sineGainDb (double freq, double fs, Fn&& fn)
{
    const int N = (int) fs * 2;
    double sc = 0, ss = 0;
    for (int i = 0; i < N; ++i)
    {
        const double y = fn (std::sin (2 * kPi * freq / fs * i));
        if (i >= N / 2) { sc += y * std::cos (2 * kPi * freq / fs * i); ss += y * std::sin (2 * kPi * freq / fs * i); }
    }
    const double amp = 2.0 * std::sqrt (sc * sc + ss * ss) / (N / 2);
    return gainToDb (amp);
}

static void testHalfband()
{
    // Stage-1 upsampler: level of the image at (fs - f) relative to the tone at f.
    // A Blackman-Harris window keeps spectral leakage out of the measurement.
    const auto c = halfband::design (16, 0.0255);
    const double fs = 44100.0;
    for (double f : { 1000.0, 10000.0, 18000.0, 20000.0 })
    {
        halfband::Up2x up; up.init (c);
        const int N = 1 << 17, settle = 4096;
        std::vector<double> y;
        for (int i = 0; i < N / 2 + settle; ++i)
        {
            double a, b;
            up.process (std::sin (2 * kPi * f / fs * i), a, b);
            if (i >= settle) { y.push_back (a); y.push_back (b); }
        }
        auto level = [&] (double freq)
        {
            double sc = 0, ss = 0;
            const double M = (double) y.size();
            for (size_t k = 0; k < y.size(); ++k)
            {
                const double t = 2 * kPi * k / M;
                const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
                const double ph = 2 * kPi * freq / (2 * fs) * (double) k;
                sc += y[k] * w * std::cos (ph); ss += y[k] * w * std::sin (ph);
            }
            return std::hypot (sc, ss);
        };
        const double rej = gainToDb (level (fs - f) / level (f), -300.0);
        check (rej < (f < 20000.0 ? -100.0 : -40.0), "halfband image rejection @" + std::to_string ((int) f) + " Hz (dB)", rej);
    }
}

static void testOversamplerPassband()
{
    for (double f : { 100.0, 1000.0, 10000.0, 18000.0, 20000.0 })
    {
        Oversampler4x o;
        const double g = sineGainDb (f, 44100.0, [&] (double x) { return o.process (x, [] (double v) { return v; }); });
        check (std::abs (g) < (f <= 18000.0 ? 0.01 : 0.1), "oversampler passband gain @" + std::to_string ((int) f) + " Hz (dB)", g);
    }
}

static void testCrossoverFlat()
{
    const double fs = 48000.0;
    for (double f : { 30.0, 150.0, 1000.0, 3500.0, 12000.0 })
    {
        LR4Crossover x1, x2; LR4Allpass ap;
        x1.setup (150.0, fs); x2.setup (3500.0, fs); ap.setup (3500.0, fs);
        const double g = sineGainDb (f, fs, [&] (double x)
        {
            double lo, rest, mid, hi;
            x1.process (x, lo, rest);
            x2.process (rest, mid, hi);
            return ap.process (lo) + mid + hi;
        });
        check (std::abs (g) < 0.001, "3-band LR4 sum flat @" + std::to_string ((int) f) + " Hz (dB)", g);
    }
}

static void testLoudness()
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m; m.prepare (fs);
        const double a = dbToGain (-20.0);
        for (int i = 0; i < (int) (fs * 10); ++i) { const double x = a * std::sin (2 * kPi * 997.0 / fs * i); m.process (x, x); }
        check (std::abs (m.getIntegrated() - (-20.0)) < 0.05, "LUFS: -20 dBFS 997 Hz stereo sine @" + std::to_string ((int) fs), m.getIntegrated());

        LoudnessMeter m2; m2.prepare (fs);
        for (int i = 0; i < (int) (fs * 10); ++i) m2.process (std::sin (2 * kPi * 997.0 / fs * i), 0.0);
        check (std::abs (m2.getIntegrated() - (-3.01)) < 0.05, "LUFS: 0 dBFS 997 Hz single channel (expect -3.01)", m2.getIntegrated());
    }
}

static double truePeak16x (const std::vector<double>& x)
{
    // Independent check: 16x band-limited interpolation with a long sinc.
    double peak = 0.0;
    const int half = 64;
    for (size_t n = (size_t) half; n + (size_t) half < x.size(); ++n)
        for (int p = 0; p < 16; ++p)
        {
            const double t = p / 16.0;
            double acc = 0.0;
            for (int k = -half + 1; k <= half; ++k)
            {
                const double d = t - k;
                const double sinc = d == 0.0 ? 1.0 : std::sin (kPi * d) / (kPi * d);
                const double w = 0.5 + 0.5 * std::cos (kPi * d / (half + 1));
                acc += x[n + (size_t) k] * sinc * w;
            }
            peak = std::max (peak, std::abs (acc));
        }
    return peak;
}

static void testLimiter()
{
    const double fs = 44100.0;
    for (double look : { 1.0, 2.0, 5.0 })
    {
        TruePeakLimiter lim; lim.prepare (fs, 10.0); lim.setLookahead (look);
        lim.setParams (-1.0, 50.0, true, true);
        std::mt19937 rng (1234);
        std::normal_distribution<double> nd (0.0, 0.5);
        Svf bl1, bl2, br1, br2; // band-limit the noise to ~0.4 fs, roughly what real program material does
        for (auto* f : { &bl1, &bl2, &br1, &br2 }) f->setup (Svf::Type::lowpass, 16000.0, 0.7071, 0.0, fs);
        std::vector<double> outL, outR;
        double samplePeak = 0.0;
        const int N = (int) fs * 3;
        for (int i = 0; i < N; ++i)
        {
            // Loud noise, high-frequency sine bursts (they produce inter-sample peaks) and single-sample spikes
            double l = bl2.process (bl1.process (nd (rng))) + 0.8 * std::sin (2 * kPi * 11025.0 / fs * i + 0.785);
            double r = br2.process (br1.process (nd (rng)));
            if (i % 9000 == 0) { l = 8.0; r = -8.0; }
            lim.process (l, r);
            outL.push_back (l); outR.push_back (r);
            samplePeak = std::max ({ samplePeak, std::abs (l), std::abs (r) });
        }
        const double ceil = dbToGain (-1.0);
        check (samplePeak <= ceil + 1e-12, "limiter sample peak <= -1 dBFS (lookahead " + std::to_string (look) + " ms)", gainToDb (samplePeak));
        const double tp = gainToDb (std::max (truePeak16x (outL), truePeak16x (outR)));
        check (tp < -1.0 + 0.2, "limiter true peak (independent 16x check) <= -0.8 dBTP", tp);
    }

    // Latency: an impulse below the threshold must come out exactly at getLatency().
    TruePeakLimiter lim; lim.prepare (fs, 10.0); lim.setLookahead (2.0); lim.setParams (-1.0, 50.0, true, true);
    int found = -1;
    for (int i = 0; i < 1000; ++i)
    {
        double l = i == 0 ? 0.5 : 0.0, r = 0.0;
        lim.process (l, r);
        if (l > 0.25) found = i;
    }
    check (found == lim.getLatency(), "limiter impulse appears at reported latency", found);
}

static void testChain()
{
    const double fs = 48000.0;
    // Null test: with every module off, the output is the input delayed by the reported latency.
    {
        MasterChain c;
        ChainParams p;
        p.eqOn = false; p.stereoOn = false; p.mbOn = false; p.compOn = false; p.satOn = false; p.limOn = false;
        c.setParams (p); c.prepare (fs);
        const int lat = c.getLatencySamples();
        std::vector<double> inL (8192), inR (8192);
        std::mt19937 rng (7); std::uniform_real_distribution<double> ud (-0.9, 0.9);
        for (size_t i = 0; i < inL.size(); ++i) { inL[i] = ud (rng); inR[i] = ud (rng); }
        auto L = inL, R = inR;
        c.process (L.data(), R.data(), (int) L.size());
        double err = 0.0;
        for (size_t i = (size_t) lat; i < L.size(); ++i)
            err = std::max ({ err, std::abs (L[i] - inL[i - (size_t) lat]), std::abs (R[i] - inR[i - (size_t) lat]) });
        check (err == 0.0, "chain bypassed = bit-exact delayed input (latency " + std::to_string (lat) + ")", err);
    }
    // Defaults: loud material, ceiling respected, no NaNs, CPU check.
    {
        MasterChain c;
        ChainParams p;
        p.mbOn = true; p.satOn = true; p.satDriveDb = 6.0; p.satWarmth = 0.5;
        p.limGainDb = 12.0;
        p.lowShelfDb = 2.0; p.highShelfDb = 1.5; p.width = 1.3;
        c.setParams (p); c.prepare (fs);
        std::mt19937 rng (3); std::normal_distribution<double> nd (0.0, 0.2);
        const int N = (int) fs * 10;
        std::vector<double> L (512), R (512);
        double peak = 0.0; bool finite = true;
        const auto t0 = std::clock();
        for (int done = 0; done < N; done += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const int n = done + i;
                const double kick = std::sin (2 * kPi * 55.0 / fs * n) * std::exp (-(n % 24000) / 3000.0);
                L[(size_t) i] = nd (rng) + kick; R[(size_t) i] = nd (rng) + kick;
            }
            c.process (L.data(), R.data(), 512);
            for (int i = 0; i < 512; ++i)
            {
                finite &= std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
                peak = std::max ({ peak, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) });
            }
        }
        const double secs = double (std::clock() - t0) / CLOCKS_PER_SEC;
        check (finite, "full chain output finite", 1);
        check (peak <= dbToGain (-1.0) + 1e-12, "full chain sample peak <= ceiling (dBFS)", gainToDb (peak));
        check (c.getMeters().truePeakMaxDb < -0.7, "full chain true peak (dBTP)", c.getMeters().truePeakMaxDb);
        std::printf ("       full chain integrated loudness: %.2f LUFS\n", c.getMeters().integrated);
        check (secs < 10.0 * 0.25, "full chain CPU: realtime fraction (one core)", secs / 10.0);
    }
}

int main()
{
    testHalfband();
    testOversamplerPassband();
    testCrossoverFlat();
    testLoudness();
    testLimiter();
    testChain();
    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
