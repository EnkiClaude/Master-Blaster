#pragma once
// The complete Master Blaster signal chain, independent of any plugin framework
// so the test suite can run it directly.
//
//  in gain -> EQ (low cut, low shelf, 2 bells, high shelf) -> stereo (width, mono bass)
//  -> 3-band compressor -> glue compressor -> saturator (4x oversampled)
//  -> true-peak limiter -> dither
//
// Everything runs in double precision.
#include "Common.h"
#include "Filters.h"
#include "Oversampling.h"
#include "Dynamics.h"
#include "Loudness.h"
#include <array>

namespace mb
{
struct ChainParams
{
    double inputGainDb = 0.0;

    bool eqOn = true;
    bool lowCutOn = true;     double lowCutHz = 25.0;
    double lowShelfHz = 90.0,  lowShelfDb = 0.0;
    double lowMidHz = 300.0,   lowMidDb = 0.0,  lowMidQ = 0.7;
    double highMidHz = 3000.0, highMidDb = 0.0, highMidQ = 0.7;
    double highShelfHz = 12000.0, highShelfDb = 0.0;

    bool stereoOn = true;
    double width = 1.0;       // 0 = mono, 1 = unchanged, 2 = double side
    bool monoBassOn = true;   double monoBassHz = 120.0;

    bool mbOn = false;
    double xoverLowHz = 150.0, xoverHighHz = 3500.0;
    std::array<double, 3> bandThreshDb { -20.0, -20.0, -20.0 };
    std::array<double, 3> bandRatio { 1.5, 1.5, 1.5 };
    std::array<double, 3> bandGainDb { 0.0, 0.0, 0.0 };
    double mbAttackMs = 20.0, mbReleaseMs = 160.0;

    bool compOn = true;
    double compThreshDb = -18.0, compRatio = 2.0, compAttackMs = 30.0, compReleaseMs = 250.0;
    double compKneeDb = 6.0, compMakeupDb = 0.0, compMix = 1.0, compScHpfHz = 90.0;

    bool satOn = false;
    double satDriveDb = 3.0, satWarmth = 0.0, satMix = 1.0;

    bool limOn = true;
    double limGainDb = 0.0, limCeilingDb = -1.0, limReleaseMs = 80.0, lookaheadMs = 2.0;
    bool autoRelease = true, truePeak = true;

    int dither = 0; // Dither::Mode
};

struct ChainMeters
{
    double inPeakDb = -150.0, outPeakDb = -150.0, truePeakMaxDb = -150.0;
    double compGrDb = 0.0, limGrDb = 0.0;
    std::array<double, 3> bandGrDb {};
    double momentary = -150.0, shortTerm = -150.0, integrated = -150.0;
    double correlation = 1.0;
};

class MasterChain
{
public:
    static constexpr double kMaxLookaheadMs = 10.0;
    static constexpr int kChunk = 32; // coefficient update granularity

    MasterChain() { satLatency = measureOversamplerDelay(); }

    void prepare (double sr)
    {
        fs = sr;
        for (auto& s : smooth) s.prepare (sr / kChunk, 20.0);
        for (auto& c : bandComp) c.prepare (sr);
        glue.prepare (sr);
        limiter.prepare (sr, kMaxLookaheadMs);
        limiter.setLookahead (params.lookaheadMs);
        currentLookahead = params.lookaheadMs;
        loud.prepare (sr);
        outTp[0] = TruePeakDetector(); outTp[1] = TruePeakDetector();
        for (auto& d : satBypassDelay) { d.prepare (satLatency + 1); d.setDelay (satLatency); }
        for (auto& d : dc) d.setup (2.0, sr);
        tpHoldDecay = msToCoef (1500.0, sr);
        reset();
        updateCoefficients (true);
    }

    void reset()
    {
        for (auto& ch : eq) for (auto& f : ch) f.reset();
        for (auto& ch : xo) for (auto& f : ch) f.reset();
        for (auto& a : xoAp) a.reset();
        for (auto& f : monoBassXo) f.reset();
        monoBassAp.reset();
        for (auto& c : bandComp) c.reset();
        glue.reset();
        for (auto& f : scHpf) f.reset();
        for (auto& o : os) o.reset();
        for (auto& d : satBypassDelay) d.reset();
        for (auto& d : dc) d.reset();
        limiter.reset();
        dither.reset();
        loud.resetAll();
        outTp[0].reset(); outTp[1].reset();
        meters = {};
        corrXY = 0.0; corrXX = corrYY = 1e-12;
    }

    void setParams (const ChainParams& p) noexcept { params = p; }
    const ChainParams& getParams() const noexcept { return params; }

    int getLatencySamples() const noexcept { return satLatency + limiter.getLatency(); }
    int getLatencyForLookahead (double ms) const noexcept
    {
        return satLatency + TruePeakDetector::kDelay + std::max (2, (int) std::round (ms * 0.001 * fs)) - 1;
    }

    // Returns true when the latency changed (new lookahead), so the host must be told.
    bool applyLookaheadIfChanged()
    {
        if (params.lookaheadMs == currentLookahead) return false;
        currentLookahead = params.lookaheadMs;
        limiter.setLookahead (currentLookahead);
        return true;
    }

    void resetLoudness() { loud.resetIntegrated(); meters.truePeakMaxDb = -150.0; }

    const ChainMeters& getMeters() const noexcept { return meters; }

    // Processes one stereo block in place. For mono, pass the same pointer twice.
    void process (double* L, double* R, int n) noexcept
    {
        double inPeak = 0.0, outPeak = 0.0;
        for (int start = 0; start < n; start += kChunk)
        {
            const int len = std::min (kChunk, n - start);
            updateCoefficients (false);
            for (int i = start; i < start + len; ++i)
            {
                double l = L[i], r = R[i];
                inPeak = std::max (inPeak, std::max (std::abs (l), std::abs (r)));
                processSample (l, r);
                L[i] = l; R[i] = r;
                outPeak = std::max (outPeak, std::max (std::abs (l), std::abs (r)));
            }
        }
        meters.inPeakDb = gainToDb (inPeak);
        meters.outPeakDb = gainToDb (outPeak);
        meters.compGrDb = glue.getReductionDb() * curWet (kWetComp);
        meters.limGrDb = limiter.getReductionDb();
        for (int b = 0; b < 3; ++b) meters.bandGrDb[(size_t) b] = bandComp[(size_t) b].getReductionDb() * curWet (kWetMb);
        meters.momentary = loud.getMomentary();
        meters.shortTerm = loud.getShortTerm();
        meters.integrated = loud.getIntegrated();
        meters.correlation = corrXY / std::sqrt (corrXX * corrYY);
    }

private:
    // Indices of the smoothed parameters.
    enum
    {
        kInGain, kLsHz, kLsDb, kLmHz, kLmDb, kLmQ, kHmHz, kHmDb, kHmQ, kHsHz, kHsDb, kLcHz,
        kWidth, kMonoHz, kXlo, kXhi, kBg0, kBg1, kBg2, kMakeup, kCompMix, kSatDrive, kSatWarm, kSatMix,
        kLimGain, kWetEq, kWetLowCut, kWetStereo, kWetMono, kWetMb, kWetComp, kWetSat, kNumSmooth
    };

    double curWet (int idx) const noexcept { return smooth[(size_t) idx].value(); }

    // Moves every smoother one chunk forward and recomputes filter coefficients.
    void updateCoefficients (bool snap) noexcept
    {
        const auto& p = params;
        const double targets[kNumSmooth] = {
            dbToGain (p.inputGainDb), std::log (p.lowShelfHz), p.lowShelfDb, std::log (p.lowMidHz), p.lowMidDb, p.lowMidQ,
            std::log (p.highMidHz), p.highMidDb, p.highMidQ, std::log (p.highShelfHz), p.highShelfDb, std::log (p.lowCutHz),
            p.width, std::log (p.monoBassHz), std::log (p.xoverLowHz), std::log (p.xoverHighHz),
            dbToGain (p.bandGainDb[0]), dbToGain (p.bandGainDb[1]), dbToGain (p.bandGainDb[2]),
            dbToGain (p.compMakeupDb), p.compMix, dbToGain (p.satDriveDb), p.satWarmth, p.satMix,
            dbToGain (p.limGainDb),
            p.eqOn ? 1.0 : 0.0, (p.eqOn && p.lowCutOn) ? 1.0 : 0.0, p.stereoOn ? 1.0 : 0.0,
            (p.stereoOn && p.monoBassOn) ? 1.0 : 0.0, p.mbOn ? 1.0 : 0.0, p.compOn ? 1.0 : 0.0, p.satOn ? 1.0 : 0.0
        };
        bool anyChange = snap;
        for (int i = 0; i < kNumSmooth; ++i)
        {
            auto& s = smooth[(size_t) i];
            if (snap) s.reset (targets[i]);
            else
            {
                const double before = s.value();
                s.setTarget (targets[i]);
                if (s.isSmoothing()) { s.advance (1); }
                anyChange |= before != s.value();
            }
        }
        if (! anyChange && coeffsValid) { setDynamicsParams(); return; }
        coeffsValid = true;

        auto v = [this] (int i) { return smooth[(size_t) i].value(); };
        for (auto& ch : eq)
        {
            ch[0].setup (Svf::Type::highpass, std::exp (v (kLcHz)), 0.54119610014619698, 0.0, fs);
            ch[1].setup (Svf::Type::highpass, std::exp (v (kLcHz)), 1.3065629648763766, 0.0, fs);
            ch[2].setup (Svf::Type::lowShelf, std::exp (v (kLsHz)), 0.7071, v (kLsDb), fs);
            ch[3].setup (Svf::Type::bell, std::exp (v (kLmHz)), v (kLmQ), v (kLmDb), fs);
            ch[4].setup (Svf::Type::bell, std::exp (v (kHmHz)), v (kHmQ), v (kHmDb), fs);
            ch[5].setup (Svf::Type::highShelf, std::exp (v (kHsHz)), 0.7071, v (kHsDb), fs);
        }
        for (auto& f : monoBassXo) f.setup (std::exp (v (kMonoHz)), fs);
        monoBassAp.setup (std::exp (v (kMonoHz)), fs);

        const double xl = std::exp (v (kXlo));
        const double xh = std::max (std::exp (v (kXhi)), xl * 1.5);
        for (auto& ch : xo) { ch[0].setup (xl, fs); ch[1].setup (xh, fs); }
        for (auto& a : xoAp) a.setup (xh, fs);
        for (auto& f : scHpf) f.setup (Svf::Type::highpass, std::max (10.0, params.compScHpfHz), 0.7071, 0.0, fs);
        setDynamicsParams();
    }

    void setDynamicsParams() noexcept
    {
        const auto& p = params;
        for (size_t b = 0; b < 3; ++b)
            bandComp[b].setParams (p.bandThreshDb[b], p.bandRatio[b], 6.0, p.mbAttackMs, p.mbReleaseMs);
        glue.setParams (p.compThreshDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, 5.0);
        limiter.setParams (p.limOn ? p.limCeilingDb : 60.0, p.limReleaseMs, p.autoRelease, p.truePeak);
        dither.setMode (p.dither);
    }

    static inline double mix (double dry, double wet, double amt) noexcept { return dry + (wet - dry) * amt; }

    inline void processSample (double& l, double& r) noexcept
    {
        auto v = [this] (int i) { return smooth[(size_t) i].value(); };

        // Every module runs all the time and is crossfaded by its "wet" amount, so
        // bypass switches are click-free and filter states are always warm.

        // Input
        const double ig = v (kInGain);
        l *= ig; r *= ig;

        // EQ
        {
            const double lc = v (kWetLowCut);
            double el = mix (l, eq[0][1].process (eq[0][0].process (l)), lc);
            double er = mix (r, eq[1][1].process (eq[1][0].process (r)), lc);
            double fl = el, fr = er;
            for (size_t k = 2; k < 6; ++k) { fl = eq[0][k].process (fl); fr = eq[1][k].process (fr); }
            const double w = v (kWetEq);
            l = mix (el, fl, w);
            r = mix (er, fr, w);
        }

        // Stereo: mid/side width plus mono bass. The side channel gets an LR4
        // high-pass; mid gets the matching allpass so M and S stay phase-aligned.
        {
            double m = 0.5 * (l + r), s = 0.5 * (l - r);
            double lo, hi;
            monoBassXo[0].process (s, lo, hi);
            const double mAp = monoBassAp.process (m);
            const double mono = v (kWetMono);
            s = mix (s, hi, mono);
            m = mix (m, mAp, mono);
            s *= mix (1.0, v (kWidth), v (kWetStereo));
            if (v (kWetStereo) > 0.0 || mono > 0.0) { l = m + s; r = m - s; }
        }

        // Three-band compressor (LR4 crossovers; the low band gets an allpass so
        // all three bands sum flat)
        {
            double bl[3], br[3], restL, restR;
            xo[0][0].process (l, bl[0], restL);
            xo[1][0].process (r, br[0], restR);
            xo[0][1].process (restL, bl[1], bl[2]);
            xo[1][1].process (restR, br[1], br[2]);
            bl[0] = xoAp[0].process (bl[0]);
            br[0] = xoAp[1].process (br[0]);
            double ml = 0.0, mr = 0.0;
            for (int b = 0; b < 3; ++b)
            {
                const double g = bandComp[(size_t) b].process (std::max (std::abs (bl[b]), std::abs (br[b]))) * v (kBg0 + b);
                ml += bl[b] * g; mr += br[b] * g;
            }
            // Fully off is bit-exact dry. The 20 ms toggle fade is the only moment
            // the crossover's allpass phase mixes with the dry signal.
            const double amt = v (kWetMb);
            l = mix (l, ml, amt); r = mix (r, mr, amt);
        }

        // Glue compressor: stereo-linked, high-passed sidechain, parallel mix
        {
            const double scl = scHpf[0].process (l), scr = scHpf[1].process (r);
            const double g = glue.process (std::max (std::abs (scl), std::abs (scr))) * v (kMakeup);
            const double amt = v (kWetComp) * v (kCompMix);
            l = mix (l, l * g, amt); r = mix (r, r * g, amt);
        }

        // Saturator: 4x oversampled. A matching delay keeps the dry path aligned,
        // so latency stays constant.
        {
            const double bl = satBypassDelay[0].process (l), br = satBypassDelay[1].process (r);
            const double d = v (kSatDrive), b = v (kSatWarm) * 0.6, m = v (kSatMix);
            const double tb = std::tanh (b);
            const double norm = 1.0 / (d * (1.0 - tb * tb));
            auto shaper = [=] (double x) { return x + ((std::tanh (d * x + b) - tb) * norm - x) * m; };
            const double sl = dc[0].process (os[0].process (l, shaper));
            const double sr = dc[1].process (os[1].process (r, shaper));
            const double wet = v (kWetSat);
            l = mix (bl, sl, wet); r = mix (br, sr, wet);
        }

        // Limiter
        const double lg = v (kLimGain);
        l *= lg; r *= lg;
        limiter.process (l, r);
        if (params.limOn)
        {
            // Sample-peak safety clamp. The limiter maths already guarantees this;
            // the clamp only catches floating-point rounding.
            const double c = dbToGainCached();
            l = std::clamp (l, -c, c); r = std::clamp (r, -c, c);
        }

        // Metering (before dither)
        loud.process (l, r);
        const double tp = std::max (outTp[0].process (l), outTp[1].process (r));
        meters.truePeakMaxDb = std::max (meters.truePeakMaxDb, gainToDb (tp));
        corrXY = corrXY * corrDecay + l * r;
        corrXX = corrXX * corrDecay + l * l;
        corrYY = corrYY * corrDecay + r * r;
        corrXX = std::max (corrXX, 1e-12); corrYY = std::max (corrYY, 1e-12);

        // Dither
        l = dither.process (l, 0); r = dither.process (r, 1);
    }

    double dbToGainCached() noexcept
    {
        if (params.limCeilingDb != cachedCeilDb) { cachedCeilDb = params.limCeilingDb; cachedCeil = dbToGain (cachedCeilDb); }
        return cachedCeil;
    }

    // Measures the oversampler's phase delay at 1 kHz (48 kHz reference) and rounds it to whole samples.
    static int measureOversamplerDelay()
    {
        Oversampler4x o;
        const double f = 1000.0 / 48000.0;
        double sc = 0.0, ss = 0.0;
        const int N = 48000;
        for (int i = 0; i < N; ++i)
        {
            const double y = o.process (std::sin (2.0 * kPi * f * i), [] (double x) { return x; });
            if (i >= N / 2) { sc += y * std::cos (2.0 * kPi * f * i); ss += y * std::sin (2.0 * kPi * f * i); }
        }
        const double phase = std::atan2 (sc, ss); // input is sin(); output is sin(w(n - D)), so phase = -wD
        const double delay = -phase / (2.0 * kPi * f);
        return std::max (0, (int) std::lround (delay));
    }

    double fs = 44100.0;
    ChainParams params;
    std::array<Smoother, kNumSmooth> smooth;
    bool coeffsValid = false;

    std::array<std::array<Svf, 6>, 2> eq;
    std::array<LR4Crossover, 1> monoBassXo;
    LR4Allpass monoBassAp;
    std::array<std::array<LR4Crossover, 2>, 2> xo;
    std::array<LR4Allpass, 2> xoAp;
    std::array<Compressor, 3> bandComp;
    Compressor glue;
    std::array<Svf, 2> scHpf;
    std::array<Oversampler4x, 2> os;
    std::array<DelayLine, 2> satBypassDelay;
    std::array<DcBlocker, 2> dc;
    int satLatency = 0;
    TruePeakLimiter limiter;
    double currentLookahead = -1.0;
    Dither dither;
    LoudnessMeter loud;
    TruePeakDetector outTp[2];
    double tpHoldDecay = 0.999;
    double corrXY = 0.0, corrXX = 1e-12, corrYY = 1e-12, corrDecay = 0.9995;
    double cachedCeilDb = 1000.0, cachedCeil = 1.0;
    ChainMeters meters;
};
} // namespace mb
