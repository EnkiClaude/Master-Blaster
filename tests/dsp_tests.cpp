// Numerical tests for the Master Blaster DSP core. They don't need JUCE.
#include "../Source/dsp/MasterChain.h"
#include <cstdio>
#include <complex>
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
        check (tp <= -1.0 + 0.01, "limiter true peak (independent 16x check) <= ceiling", tp);
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
        p.eqOn = false; p.stereoOn = false; p.mbOn = false; p.dynOn = false; p.compOn = false; p.satOn = false; p.limOn = false;
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
        p.mbOn = true; p.dynOn = true; p.satOn = true; p.satDriveDb = 6.0; p.satWarmth = 0.5; p.compAutoRelease = true; p.compAutoMakeup = true;
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
        check (c.getMeters().truePeakMaxDb <= -1.0 + 0.01, "full chain true peak <= ceiling (dBTP)", c.getMeters().truePeakMaxDb);
        std::printf ("       full chain integrated loudness: %.2f LUFS\n", c.getMeters().integrated);
        check (secs < 10.0 * 0.25, "full chain CPU: realtime fraction (one core)", secs / 10.0);
    }
}


static void testMatchedEq()
{
    // Digital response must track the analog prototype right up to 20 kHz at 44.1 kHz.
    const double fs = 44100.0;
    struct Case { MatchedBiquad::Type t; double fc, q, db; const char* name; };
    const Case cases[] = { { MatchedBiquad::Type::highShelf, 12000, 0.7071, 6, "air shelf +6 dB @12k" },
                           { MatchedBiquad::Type::highShelf, 16000, 0.7071, -6, "air shelf -6 dB @16k" },
                           { MatchedBiquad::Type::bell, 10000, 4.0, -9, "bell -9 dB Q4 @10k" },
                           { MatchedBiquad::Type::lowShelf, 90, 0.7071, 4, "low shelf +4 dB @90" },
                           { MatchedBiquad::Type::bell, 300, 0.7, 3, "bell +3 dB @300" } };
    for (const auto& c : cases)
    {
        MatchedBiquad b; b.setup (c.t, c.fc, c.q, c.db, fs);
        const double A = std::pow (10.0, c.db / 40.0), sA = std::sqrt (A);
        double worst = 0.0;
        for (double f = 20.0; f < 20000.0; f *= 1.02)
        {
            const std::complex<double> s (0.0, f / c.fc);
            std::complex<double> h;
            if (c.t == MatchedBiquad::Type::bell) h = (s * s + s * (A / c.q) + 1.0) / (s * s + s / (A * c.q) + 1.0);
            else if (c.t == MatchedBiquad::Type::lowShelf) h = A * (s * s + s * (sA / c.q) + A) / (A * s * s + s * (sA / c.q) + 1.0);
            else h = A * (A * s * s + s * (sA / c.q) + 1.0) / (s * s + s * (sA / c.q) + A);
            worst = std::max (worst, std::abs (b.magnitudeDb (f, fs) - 20.0 * std::log10 (std::abs (h))));
        }
        check (worst < 0.5, std::string ("matched EQ vs analog, ") + c.name + " (worst dB)", worst);
    }
}

static void testTruePeakDetector()
{
    // Sum of 40 random sines up to 0.45 fs; compare against the exact continuous peak.
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> u (0, 1);
    double worst = 0.0;
    for (int trial = 0; trial < 3; ++trial)
    {
        double f[40], ph[40];
        for (int k = 0; k < 40; ++k) { f[k] = u (rng) * 0.45; ph[k] = u (rng) * 6.28; }
        auto sig = [&] (double t) { double s = 0; for (int k = 0; k < 40; ++k) s += 0.1 * std::sin (2 * kPi * f[k] * t + ph[k]); return s; };
        TruePeakDetector d;
        std::vector<double> det;
        for (int i = 0; i < 2000; ++i) det.push_back (d.process (sig (i)));
        for (int n = 100; n < 1900; ++n)
        {
            double tp = 0;
            for (int j = 0; j <= 128; ++j) tp = std::max (tp, std::abs (sig (n + j / 128.0)));
            worst = std::max (worst, gainToDb (tp) - gainToDb (det[(size_t) (n + TruePeakDetector::kDelay)]));
        }
    }
    check (worst < 0.06, "true-peak detector worst underestimate, content to 0.45 fs (dB)", worst);
}

// Runs a chain on a stereo generator and returns the output (after `skip` samples).
template <typename Gen>
static void runChain (MasterChain& c, Gen&& gen, int n, std::vector<double>& outL, std::vector<double>& outR, int skip = 0)
{
    std::vector<double> L (256), R (256);
    for (int done = 0; done < n; done += 256)
    {
        for (int i = 0; i < 256; ++i) gen (done + i, L[(size_t) i], R[(size_t) i]);
        c.process (L.data(), R.data(), 256);
        for (int i = 0; i < 256; ++i)
            if (done + i >= skip) { outL.push_back (L[(size_t) i]); outR.push_back (R[(size_t) i]); }
    }
}

static ChainParams allOff()
{
    ChainParams p;
    p.eqOn = false; p.stereoOn = false; p.mbOn = false; p.dynOn = false; p.compOn = false; p.satOn = false; p.limOn = false;
    return p;
}

static double rmsDb (const std::vector<double>& x)
{
    double e = 0; for (double v : x) e += v * v;
    return gainToDb (std::sqrt (e / std::max<size_t> (1, x.size())));
}

static void testDynamicEq()
{
    const double fs = 48000.0;
    auto level = [&] (double freq, double ampDb)
    {
        MasterChain c; auto p = allOff();
        p.dynOn = true; p.dynHz = { 3000.0, 3000.0 }; p.dynQ = { 2.0, 2.0 };
        p.dynThreshDb = { -30.0, 0.0 }; p.dynRangeDb = { -6.0, 0.0 };
        c.setParams (p); c.prepare (fs);
        const double a = dbToGain (ampDb);
        std::vector<double> L, R;
        runChain (c, [&] (int i, double& l, double& r) { l = r = a * std::sin (2 * kPi * freq / fs * i); }, (int) fs, L, R, (int) fs / 2);
        return rmsDb (L) - gainToDb (a / std::sqrt (2.0));
    };
    const double loud = level (3000.0, -6.0), quiet = level (3000.0, -50.0), offBand = level (300.0, -6.0);
    check (loud < -5.0 && loud > -6.5, "dynamic EQ: loud in-band tone cut by ~range (dB)", loud);
    check (std::abs (quiet) < 0.05, "dynamic EQ: quiet in-band tone untouched (dB)", quiet);
    check (std::abs (offBand) < 0.3, "dynamic EQ: loud off-band tone barely touched (dB)", offBand);
}

static void testModesAndCompare()
{
    const double fs = 48000.0;
    // Mono input through mid-only and side-only EQ and multiband must stay mono (L == R).
    for (int mode : { 1, 2 })
    {
        MasterChain c; auto p = allOff();
        p.eqOn = true; p.eqMode = mode; p.highShelfDb = 6.0; p.lowShelfDb = -3.0;
        p.mbOn = true; p.mbMode = mode; p.bandThreshDb = { -30.0, -30.0, -30.0 }; p.bandRatio = { 3.0, 3.0, 3.0 };
        c.setParams (p); c.prepare (fs);
        std::mt19937 rng (2); std::normal_distribution<double> nd (0.0, 0.2);
        std::vector<double> L, R;
        runChain (c, [&] (int, double& l, double& r) { l = r = nd (rng); }, 48000, L, R);
        double diff = 0; for (size_t i = 0; i < L.size(); ++i) diff = std::max (diff, std::abs (L[i] - R[i]));
        check (diff < 1e-12, std::string ("mono input stays mono, ") + (mode == 1 ? "mid" : "side") + "-only EQ + multiband", diff);
    }
    // Side-only processing must leave a mono signal's level unchanged (it has no side).
    {
        MasterChain c; auto p = allOff();
        p.eqOn = true; p.eqMode = 2; p.highShelfDb = 9.0;
        c.setParams (p); c.prepare (fs);
        std::vector<double> L, R;
        runChain (c, [&] (int i, double& l, double& r) { l = r = 0.3 * std::sin (2 * kPi * 9000.0 / fs * i); }, 48000, L, R, 24000);
        const double d = rmsDb (L) - gainToDb (0.3 / std::sqrt (2.0));
        check (std::abs (d) < 0.01, "side-only air shelf leaves mono content untouched (dB)", d);
    }
    // Delta with every module off: once the level match settles, output ~ silence.
    {
        MasterChain c; auto p = allOff(); p.compare = 2;
        c.setParams (p); c.prepare (fs);
        std::mt19937 rng (4); std::normal_distribution<double> nd (0.0, 0.2);
        std::vector<double> L, R;
        runChain (c, [&] (int, double& l, double& r) { l = nd (rng); r = nd (rng); }, (int) fs * 12, L, R, (int) fs * 10);
        check (rmsDb (L) < -100.0, "delta of a transparent chain is silent (dBFS RMS)", rmsDb (L));
    }
    // Level-matched bypass: the bypassed signal plays at the processed loudness.
    {
        auto loudness = [&] (int compare)
        {
            MasterChain c; ChainParams p; p.limGainDb = 8.0; p.compare = compare;
            c.setParams (p); c.prepare (fs);
            std::mt19937 rng (6); std::normal_distribution<double> nd (0.0, 0.1);
            LoudnessMeter m; m.prepare (fs);
            std::vector<double> L, R;
            runChain (c, [&] (int i, double& l, double& r) { const double k = 0.5 * std::sin (2 * kPi * 60.0 / fs * i); l = nd (rng) + k; r = nd (rng) + k; },
                      (int) fs * 14, L, R, (int) fs * 8);
            for (size_t i = 0; i < L.size(); ++i) m.process (L[i], R[i]);
            return m.getIntegrated();
        };
        const double processed = loudness (0), bypass = loudness (1);
        check (std::abs (processed - bypass) < 0.5, "level-matched bypass within 0.5 LU of processed (LU diff)", processed - bypass);
    }
    // Auto makeup: glue compression with auto makeup keeps loudness close to the uncompressed level.
    {
        auto loudness = [&] (bool compOn)
        {
            MasterChain c; auto p = allOff(); p.compOn = compOn; p.compThreshDb = -30.0; p.compRatio = 3.0; p.compAutoMakeup = true;
            c.setParams (p); c.prepare (fs);
            std::mt19937 rng (8); std::normal_distribution<double> nd (0.0, 0.15);
            LoudnessMeter m; m.prepare (fs);
            std::vector<double> L, R;
            runChain (c, [&] (int, double& l, double& r) { l = nd (rng); r = nd (rng); }, (int) fs * 16, L, R, (int) fs * 10);
            for (size_t i = 0; i < L.size(); ++i) m.process (L[i], R[i]);
            return m.getIntegrated();
        };
        const double d = loudness (true) - loudness (false);
        check (std::abs (d) < 1.0, "glue auto makeup keeps loudness within 1 LU (LU diff)", d);
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
    testMatchedEq();
    testTruePeakDetector();
    testDynamicEq();
    testModesAndCompare();
    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
