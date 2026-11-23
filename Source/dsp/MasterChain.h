#pragma once
// The complete Master Blaster signal chain, independent of any plugin framework
// so the test suite can run it directly.
//
//  in gain -> EQ (low cut + 4 matched bands; stereo / mid / side) -> stereo (width, mono bass)
//  -> 3-band compressor (stereo / mid / side) -> 2-band dynamic EQ -> glue compressor
//  -> saturator (4x oversampled) -> true-peak limiter -> compare (level-matched bypass / delta)
//  -> dither
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
    int eqMode = 0;           // 0 stereo, 1 mid only, 2 side only

    bool stereoOn = true;
    double width = 1.0;       // 0 = mono, 1 = unchanged, 2 = double side
    bool monoBassOn = true;   double monoBassHz = 120.0;

    bool mbOn = false;
    double xoverLowHz = 150.0, xoverHighHz = 3500.0;
    std::array<double, 3> bandThreshDb { -20.0, -20.0, -20.0 };
    std::array<double, 3> bandRatio { 1.5, 1.5, 1.5 };
    std::array<double, 3> bandGainDb { 0.0, 0.0, 0.0 };
    double mbAttackMs = 20.0, mbReleaseMs = 160.0;
    int mbMode = 0;           // 0 stereo, 1 mid only, 2 side only

    bool dynOn = false;
    std::array<double, 2> dynHz { 250.0, 3200.0 }, dynQ { 1.5, 2.0 }, dynThreshDb { -24.0, -24.0 }, dynRangeDb { -4.0, -4.0 };
    double dynAttackMs = 5.0, dynReleaseMs = 120.0;

    bool compOn = true;
    double compThreshDb = -18.0, compRatio = 2.0, compAttackMs = 30.0, compReleaseMs = 250.0;
    double compKneeDb = 6.0, compMakeupDb = 0.0, compMix = 1.0, compScHpfHz = 90.0;
    bool compAutoRelease = false, compAutoMakeup = false;

    bool satOn = false;
    double satDriveDb = 3.0, satWarmth = 0.0, satMix = 1.0;

    bool limOn = true;
    double limGainDb = 0.0, limCeilingDb = -1.0, limReleaseMs = 80.0, lookaheadMs = 2.0;
    bool autoRelease = true, truePeak = true;

    int dither = 0; // Dither::Mode
    int compare = 0; // 0 processed, 1 level-matched bypass, 2 delta (processed minus level-matched dry)
};

struct ChainMeters
{
    double inPeakDb = -150.0, outPeakDb = -150.0, truePeakMaxDb = -150.0;
    double compGrDb = 0.0, limGrDb = 0.0;
    std::array<double, 3> bandGrDb {};
    std::array<double, 2> dynGrDb {};
    double autoMakeupDb = 0.0, matchGainDb = 0.0;
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
        for (auto& set : bandComp) for (auto& c : set) c.prepare (sr);
        for (auto& c : dynComp) c.prepare (sr);
        glue.prepare (sr);
        limiter.prepare (sr, kMaxLookaheadMs);
        limiter.setLookahead (params.lookaheadMs);
        currentLookahead = params.lookaheadMs;
        loud.prepare (sr);
        for (auto& d : satBypassDelay) { d.prepare (satLatency + 1); d.setDelay (satLatency); }
        for (auto& d : dc) d.setup (2.0, sr);
        const int maxLatency = satLatency + TruePeakDetector::kDelay + (int) std::ceil (kMaxLookaheadMs * 0.001 * sr) + 4;
        for (auto& d : dryDelay) { d.prepare (maxLatency); d.setDelay (getLatencySamples()); }
        setupKWeighting (sr);
        autoMakeupCoef = msToCoef (2500.0, sr);
        matchCoef = msToCoef (3000.0, sr);
        corrDecay = msToCoef (300.0, sr);
        reset();
        updateCoefficients (true);
    }

    void reset()
    {
        for (auto& ch : lowCut) for (auto& f : ch) f.reset();
        for (auto& ch : eq) for (auto& f : ch) f.reset();
        for (auto& ch : xo) for (auto& f : ch) f.reset();
        for (auto& a : xoAp) a.reset();
        for (auto& ch : msAp) for (auto& a : ch) a.reset();
        monoBassXo.reset();
        monoBassAp.reset();
        for (auto& set : bandComp) for (auto& c : set) c.reset();
        for (auto& b : dynBand) b.reset();
        for (auto& c : dynComp) c.reset();
        glue.reset();
        for (auto& f : scHpf) f.reset();
        for (auto& o : os) o.reset();
        for (auto& d : satBypassDelay) d.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : dryDelay) d.reset();
        for (auto& k : kw) k.reset();
        limiter.reset();
        dither.reset();
        loud.resetAll();
        outTp[0].reset(); outTp[1].reset();
        meters = {};
        corrXY = 0.0; corrXX = corrYY = 1e-12;
        autoMakeupDb = 0.0;
        eDry = eWet = 0.0; matchGain = 1.0;
    }

    void setParams (const ChainParams& p) noexcept { params = p; }
    const ChainParams& getParams() const noexcept { return params; }

    int getLatencySamples() const noexcept { return satLatency + limiter.getLatency(); }

    // Returns true when the latency changed (new lookahead), so the host must be told.
    bool applyLookaheadIfChanged()
    {
        if (params.lookaheadMs == currentLookahead) return false;
        currentLookahead = params.lookaheadMs;
        limiter.setLookahead (currentLookahead);
        for (auto& d : dryDelay) { d.setDelay (getLatencySamples()); d.reset(); }
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
        meters.compGrDb = glue.getReductionDb() * v (kWetComp);
        meters.limGrDb = limiter.getReductionDb();
        for (size_t b = 0; b < 3; ++b)
            meters.bandGrDb[b] = v (kWetMb) * (v (kMbSt) * bandComp[0][b].getReductionDb() + v (kMbMid) * bandComp[1][b].getReductionDb()
                                              + v (kMbSide) * bandComp[2][b].getReductionDb());
        for (size_t b = 0; b < 2; ++b) meters.dynGrDb[b] = std::min (dynComp[b].getReductionDb(), -params.dynRangeDb[b]) * v (kWetDyn);
        meters.autoMakeupDb = params.compAutoMakeup ? autoMakeupDb : 0.0;
        meters.matchGainDb = gainToDb (matchGain);
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
        kLimGain, kDyn0Hz, kDyn1Hz, kDyn0Q, kDyn1Q,
        kWetEq, kWetLowCut, kWetStereo, kWetMono, kWetMb, kWetDyn, kWetComp, kWetSat, kWetAutoMakeup,
        kEqSt, kEqMid, kEqSide, kMbSt, kMbMid, kMbSide, kCmpOff, kCmpByp, kCmpDelta, kNumSmooth
    };

    inline double v (int i) const noexcept { return smooth[(size_t) i].value(); }

    // Moves every smoother one chunk forward and recomputes filter coefficients.
    void updateCoefficients (bool snap) noexcept
    {
        const auto& p = params;
        auto oneHot = [] (int sel, int idx) { return sel == idx ? 1.0 : 0.0; };
        const double targets[kNumSmooth] = {
            dbToGain (p.inputGainDb), std::log (p.lowShelfHz), p.lowShelfDb, std::log (p.lowMidHz), p.lowMidDb, p.lowMidQ,
            std::log (p.highMidHz), p.highMidDb, p.highMidQ, std::log (p.highShelfHz), p.highShelfDb, std::log (p.lowCutHz),
            p.width, std::log (p.monoBassHz), std::log (p.xoverLowHz), std::log (p.xoverHighHz),
            dbToGain (p.bandGainDb[0]), dbToGain (p.bandGainDb[1]), dbToGain (p.bandGainDb[2]),
            dbToGain (p.compMakeupDb), p.compMix, dbToGain (p.satDriveDb), p.satWarmth, p.satMix,
            dbToGain (p.limGainDb), std::log (p.dynHz[0]), std::log (p.dynHz[1]), p.dynQ[0], p.dynQ[1],
            p.eqOn ? 1.0 : 0.0, (p.eqOn && p.lowCutOn) ? 1.0 : 0.0, p.stereoOn ? 1.0 : 0.0,
            (p.stereoOn && p.monoBassOn) ? 1.0 : 0.0, p.mbOn ? 1.0 : 0.0, p.dynOn ? 1.0 : 0.0, p.compOn ? 1.0 : 0.0,
            p.satOn ? 1.0 : 0.0, (p.compOn && p.compAutoMakeup) ? 1.0 : 0.0,
            oneHot (p.eqMode, 0), oneHot (p.eqMode, 1), oneHot (p.eqMode, 2),
            oneHot (p.mbMode, 0), oneHot (p.mbMode, 1), oneHot (p.mbMode, 2),
            oneHot (p.compare, 0), oneHot (p.compare, 1), oneHot (p.compare, 2)
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
                if (s.isSmoothing()) s.advance (1);
                anyChange |= before != s.value();
            }
        }
        setDynamicsParams();
        if (! anyChange && coeffsValid) return;
        coeffsValid = true;

        for (auto& ch : lowCut)
        {
            ch[0].setup (Svf::Type::highpass, std::exp (v (kLcHz)), 0.54119610014619698, 0.0, fs);
            ch[1].setup (Svf::Type::highpass, std::exp (v (kLcHz)), 1.3065629648763766, 0.0, fs);
        }
        using T = MatchedBiquad::Type;
        for (auto& ch : eq)
        {
            ch[0].setup (T::lowShelf, std::exp (v (kLsHz)), 0.7071, v (kLsDb), fs);
            ch[1].setup (T::bell, std::exp (v (kLmHz)), v (kLmQ), v (kLmDb), fs);
            ch[2].setup (T::bell, std::exp (v (kHmHz)), v (kHmQ), v (kHmDb), fs);
            ch[3].setup (T::highShelf, std::exp (v (kHsHz)), 0.7071, v (kHsDb), fs);
        }
        monoBassXo.setup (std::exp (v (kMonoHz)), fs);
        monoBassAp.setup (std::exp (v (kMonoHz)), fs);

        const double xl = std::exp (v (kXlo));
        const double xh = std::max (std::exp (v (kXhi)), xl * 1.5);
        for (auto& ch : xo) { ch[0].setup (xl, fs); ch[1].setup (xh, fs); }
        for (auto& a : xoAp) a.setup (xh, fs);
        for (auto& ch : msAp) { ch[0].setup (xl, fs); ch[1].setup (xh, fs); }
        for (int b = 0; b < 2; ++b) dynBand[(size_t) b].setup (std::exp (v (kDyn0Hz + b)), v (kDyn0Q + b), fs);
        for (auto& f : scHpf) f.setup (Svf::Type::highpass, std::max (10.0, params.compScHpfHz), 0.7071, 0.0, fs);
    }

    void setDynamicsParams() noexcept
    {
        const auto& p = params;
        for (auto& set : bandComp)
            for (size_t b = 0; b < 3; ++b)
                set[b].setParams (p.bandThreshDb[b], p.bandRatio[b], 6.0, p.mbAttackMs, p.mbReleaseMs);
        for (size_t b = 0; b < 2; ++b)
        {
            dynComp[b].setParams (p.dynThreshDb[b], 4.0, 6.0, p.dynAttackMs, p.dynReleaseMs);
            dynFloor[b] = dbToGain (std::min (0.0, p.dynRangeDb[b])); // the most the band may cut
        }
        glue.setParams (p.compThreshDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, 5.0, p.compAutoRelease);
        limiter.setParams (p.limOn ? p.limCeilingDb : 60.0, p.limReleaseMs, p.autoRelease, p.truePeak);
        dither.setMode (p.dither);
    }

    static inline double mix (double dry, double wet, double amt) noexcept { return dry + (wet - dry) * amt; }

    // 3-band split of one channel (index ch). The low band gets the high
    // crossover's allpass so the three bands sum flat.
    inline void split (int ch, double x, double* b) noexcept
    {
        double rest;
        xo[(size_t) ch][0].process (x, b[0], rest);
        xo[(size_t) ch][1].process (rest, b[1], b[2]);
        b[0] = xoAp[(size_t) ch].process (b[0]);
    }

    inline void processSample (double& l, double& r) noexcept
    {
        // Every module runs all the time and is crossfaded by its "wet" amount, so
        // bypass switches are click-free and filter states are always warm.
        const double inL = l, inR = r;

        // Input
        const double ig = v (kInGain);
        l *= ig; r *= ig;

        // EQ: the low cut works on L/R; the four matched bands run as stereo, mid-only or side-only
        {
            const double lc = v (kWetLowCut);
            const double el = mix (l, lowCut[0][1].process (lowCut[0][0].process (l)), lc);
            const double er = mix (r, lowCut[1][1].process (lowCut[1][0].process (r)), lc);
            const double m = 0.5 * (el + er), s = 0.5 * (el - er);
            double c[4] = { el, er, m, s }; // L, R, M, S
            for (size_t ch = 0; ch < 4; ++ch)
                for (auto& f : eq[ch]) c[ch] = f.process (c[ch]);
            const double wSt = v (kEqSt), wM = v (kEqMid), wS = v (kEqSide);
            const double fl = wSt * c[0] + wM * (c[2] + s) + wS * (m + c[3]);
            const double fr = wSt * c[1] + wM * (c[2] - s) + wS * (m - c[3]);
            const double w = v (kWetEq);
            l = mix (el, fl, w);
            r = mix (er, fr, w);
        }

        // Stereo: mid/side width plus mono bass. The side channel gets an LR4
        // high-pass; mid gets the matching allpass so M and S stay phase-aligned.
        {
            double m = 0.5 * (l + r), s = 0.5 * (l - r);
            double lo, hi;
            monoBassXo.process (s, lo, hi);
            const double mAp = monoBassAp.process (m);
            const double mono = v (kWetMono);
            s = mix (s, hi, mono);
            m = mix (m, mAp, mono);
            s *= mix (1.0, v (kWidth), v (kWetStereo));
            if (v (kWetStereo) > 0.0 || mono > 0.0) { l = m + s; r = m - s; }
        }

        // Three-band compressor (stereo-linked on L/R, or on mid or side alone).
        // In mid or side mode the untouched channel goes through the same allpass
        // pair as the band sum, so M and S stay phase-coherent.
        {
            const double m = 0.5 * (l + r), s = 0.5 * (l - r);
            double bl[3], br[3], bm[3], bs[3];
            split (0, l, bl); split (1, r, br); split (2, m, bm); split (3, s, bs);
            const double mAp = msAp[0][1].process (msAp[0][0].process (m));
            const double sAp = msAp[1][1].process (msAp[1][0].process (s));
            double sl = 0, sr = 0, mm = 0, ss = 0;
            for (size_t b = 0; b < 3; ++b)
            {
                const double bg = v (kBg0 + (int) b);
                sl += bl[b] * (bandComp[0][b].process (std::max (std::abs (bl[b]), std::abs (br[b]))) * bg);
                sr += br[b] * (bandComp[0][b].getLastGain() * bg);
                mm += bm[b] * (bandComp[1][b].process (bm[b]) * bg);
                ss += bs[b] * (bandComp[2][b].process (bs[b]) * bg);
            }
            const double wSt = v (kMbSt), wM = v (kMbMid), wS = v (kMbSide);
            const double ol = wSt * sl + wM * (mm + sAp) + wS * (mAp + ss);
            const double orr = wSt * sr + wM * (mm - sAp) + wS * (mAp - ss);
            // Fully off is bit-exact dry. The 20 ms toggle fade is the only moment
            // the crossover's allpass phase mixes with the dry signal.
            const double amt = v (kWetMb);
            l = mix (l, ol, amt); r = mix (r, orr, amt);
        }

        // Dynamic EQ: two constant-Q bells that cut only while their band is loud
        {
            double dl = l, dr = r;
            for (size_t b = 0; b < 2; ++b)
            {
                const double bpl = dynBand[b].bandpass (0, dl), bpr = dynBand[b].bandpass (1, dr);
                double g = dynComp[b].process (std::max (std::abs (bpl), std::abs (bpr)));
                g = std::max (g, dynFloor[b]);
                dl += (g - 1.0) * bpl;
                dr += (g - 1.0) * bpr;
            }
            const double amt = v (kWetDyn);
            l = mix (l, dl, amt); r = mix (r, dr, amt);
        }

        // Glue compressor: stereo-linked, high-passed sidechain, parallel mix,
        // optional auto release and auto makeup
        {
            const double scl = scHpf[0].process (l), scr = scHpf[1].process (r);
            const double gr = glue.process (std::max (std::abs (scl), std::abs (scr)));
            // Auto makeup adds back the long-term average reduction, so loudness stays
            // about the same while the dynamics change.
            const double effGrDb = glue.getReductionDb() * v (kCompMix);
            autoMakeupDb = effGrDb + (autoMakeupDb - effGrDb) * autoMakeupCoef;
            const double autoG = 1.0 + (dbToGainFast (autoMakeupDb) - 1.0) * v (kWetAutoMakeup);
            const double g = gr * v (kMakeup) * autoG;
            const double gTot = mix (1.0, g, v (kWetComp) * v (kCompMix));
            l *= gTot; r *= gTot;
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

        // Metering of the processed signal (before compare and dither)
        loud.process (l, r);
        const double tp = std::max (outTp[0].process (l), outTp[1].process (r));
        meters.truePeakMaxDb = std::max (meters.truePeakMaxDb, gainToDb (tp));
        corrXY = corrXY * corrDecay + l * r;
        corrXX = corrXX * corrDecay + l * l;
        corrYY = corrYY * corrDecay + r * r;
        corrXX = std::max (corrXX, 1e-12); corrYY = std::max (corrYY, 1e-12);

        // Compare: the dry input, delayed to line up with the output and scaled to
        // the same loudness (K-weighted, ~3 s average), so A/B is about tone and
        // dynamics, not volume.
        {
            const double dl = dryDelay[0].process (inL), dr = dryDelay[1].process (inR);
            const double kd = kWeighted (0, dl) + kWeighted (1, dr);
            const double kwOut = kWeighted (2, l) + kWeighted (3, r);
            eDry = kd + (eDry - kd) * matchCoef;
            eWet = kwOut + (eWet - kwOut) * matchCoef;
            if (eDry > 1.0e-9 && eWet > 1.0e-9)
            {
                const double target = std::clamp (std::sqrt (eWet / eDry), 0.03, 32.0);
                matchGain = target + (matchGain - target) * matchSmooth;
            }
            const double mdl = dl * matchGain, mdr = dr * matchGain;
            const double wOff = v (kCmpOff), wByp = v (kCmpByp), wDel = v (kCmpDelta);
            if (wOff < 1.0)
            {
                const double ol = wOff * l + wByp * mdl + wDel * (l - mdl);
                const double orr = wOff * r + wByp * mdr + wDel * (r - mdr);
                l = ol; r = orr;
            }
        }

        // Dither
        l = dither.process (l, 0); r = dither.process (r, 1);
    }

    // K-weighting filters for the four compare-loudness channels (dry L/R, wet L/R); returns the squared output.
    inline double kWeighted (int ch, double x) noexcept
    {
        const double y = kw[(size_t) ch * 2 + 1].process (kw[(size_t) ch * 2].process (x));
        return y * y;
    }

    void setupKWeighting (double sr)
    {
        // Same coefficients as LoudnessMeter (BS.1770-4, sample-rate independent derivation)
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        for (size_t i = 0; i < kw.size(); i += 2)
            kw[i].set ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                       2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        f0 = 38.13547087602444; Q = 0.5003270373238773;
        K = std::tan (kPi * f0 / sr);
        a0 = 1.0 + K / Q + K * K;
        for (size_t i = 1; i < kw.size(); i += 2)
            kw[i].set (1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        matchSmooth = msToCoef (50.0, sr);
    }

    double dbToGainCached() noexcept
    {
        if (params.limCeilingDb != cachedCeilDb) { cachedCeilDb = params.limCeilingDb; cachedCeil = dbToGain (cachedCeilDb); }
        return cachedCeil;
    }

    // exp-based dB to gain (identical maths to dbToGain, slightly cheaper)
    static inline double dbToGainFast (double db) noexcept { return std::exp (db * 0.11512925464970229); }

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

    std::array<std::array<Svf, 2>, 2> lowCut;
    std::array<std::array<MatchedBiquad, 4>, 4> eq;       // channels L, R, M, S
    LR4Crossover monoBassXo;
    LR4Allpass monoBassAp;
    std::array<std::array<LR4Crossover, 2>, 4> xo;         // channels L, R, M, S
    std::array<LR4Allpass, 4> xoAp;
    std::array<std::array<LR4Allpass, 2>, 2> msAp;         // allpass pairs for the untouched M or S
    std::array<std::array<Compressor, 3>, 3> bandComp;     // [stereo, mid, side][band]
    std::array<DynamicBand, 2> dynBand;
    std::array<Compressor, 2> dynComp;
    std::array<double, 2> dynFloor { 0.5, 0.5 };
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
    std::array<DelayLine, 2> dryDelay;
    std::array<Biquad, 8> kw;
    double eDry = 0.0, eWet = 0.0, matchGain = 1.0, matchCoef = 0.9999, matchSmooth = 0.999;
    double autoMakeupDb = 0.0, autoMakeupCoef = 0.9999;
    double corrXY = 0.0, corrXX = 1e-12, corrYY = 1e-12, corrDecay = 0.9995;
    double cachedCeilDb = 1000.0, cachedCeil = 1.0;
    ChainMeters meters;
};
} // namespace mb
