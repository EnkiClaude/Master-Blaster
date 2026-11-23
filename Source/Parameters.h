#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MasterChain.h"

namespace mbp
{
using APVTS = juce::AudioProcessorValueTreeState;

inline const juce::StringArray lookaheadChoices { "0.5 ms", "1 ms", "2 ms", "3 ms", "5 ms" };
inline constexpr double lookaheadMs[] { 0.5, 1.0, 2.0, 3.0, 5.0 };
inline const juce::StringArray ditherChoices { "Off", "24-bit TPDF", "16-bit TPDF", "16-bit shaped" };

inline juce::NormalisableRange<float> freqRange (float lo, float hi)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

inline juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

// Display formatting for each unit (what the host and the knob text boxes show).
inline std::function<juce::String (float, int)> formatter (const juce::String& unit)
{
    using juce::String;
    if (unit == "Hz")
        return [] (float x, int) { return x < 1000.0f ? String (x, x < 100.0f ? 1 : 0) + " Hz" : String (x / 1000.0f, 2) + " kHz"; };
    if (unit == "ms")
        return [] (float x, int) { return String (x, x < 10.0f ? 2 : (x < 100.0f ? 1 : 0)) + " ms"; };
    if (unit == "%")
        return [] (float x, int) { return String (juce::roundToInt (x)) + " %"; };
    if (unit == ":1")
        return [] (float x, int) { return String (x, 1) + ":1"; };
    if (unit.isEmpty())
        return [] (float x, int) { return String (x, 2); };
    return [unit] (float x, int) { return String (x, unit == "dB" ? 1 : 1) + " " + unit; };
}

inline APVTS::ParameterLayout createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
    const int v = 1; // parameter version hint
    auto fl = [&] (const String& id, const String& name, NormalisableRange<float> range, float def, const String& unit)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, range, def,
                                                             AudioParameterFloatAttributes()
                                                                 .withLabel (unit)
                                                                 .withStringFromValueFunction (formatter (unit))));
    };
    auto bo = [&] (const char* id, const char* name, bool def)
    { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, v }, name, def)); };
    auto ch = [&] (const char* id, const char* name, const StringArray& c, int def)
    { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, v }, name, c, def)); };

    fl ("inGain", "Input Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB");

    bo ("eqOn", "EQ On", true);
    bo ("lowCutOn", "Low Cut On", true);
    fl ("lowCutHz", "Low Cut Freq", freqRange (10.0f, 120.0f), 25.0f, "Hz");
    fl ("lowShelfHz", "Low Shelf Freq", freqRange (30.0f, 400.0f), 90.0f, "Hz");
    fl ("lowShelfDb", "Low Shelf Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    fl ("lowMidHz", "Low Mid Freq", freqRange (80.0f, 2000.0f), 300.0f, "Hz");
    fl ("lowMidDb", "Low Mid Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    fl ("lowMidQ", "Low Mid Q", skewed (0.2f, 6.0f, 1.0f), 0.7f, "");
    fl ("highMidHz", "High Mid Freq", freqRange (600.0f, 12000.0f), 3000.0f, "Hz");
    fl ("highMidDb", "High Mid Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    fl ("highMidQ", "High Mid Q", skewed (0.2f, 6.0f, 1.0f), 0.7f, "");
    fl ("highShelfHz", "High Shelf Freq", freqRange (2000.0f, 20000.0f), 12000.0f, "Hz");
    fl ("highShelfDb", "High Shelf Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");

    bo ("stereoOn", "Stereo On", true);
    fl ("width", "Stereo Width", { 0.0f, 200.0f, 0.1f }, 100.0f, "%");
    bo ("monoBassOn", "Mono Bass On", true);
    fl ("monoBassHz", "Mono Bass Freq", freqRange (20.0f, 300.0f), 120.0f, "Hz");

    bo ("mbOn", "Multiband On", false);
    fl ("xoverLow", "Crossover Low", freqRange (40.0f, 600.0f), 150.0f, "Hz");
    fl ("xoverHigh", "Crossover High", freqRange (1000.0f, 12000.0f), 3500.0f, "Hz");
    const char* bandNames[] { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const String n (bandNames[b]);
        const auto tId = "band" + String (b) + "Thresh", rId = "band" + String (b) + "Ratio", gId = "band" + String (b) + "Gain";
        fl (tId, n + " Threshold", { -48.0f, 0.0f, 0.01f }, -20.0f, "dB");
        fl (rId, n + " Ratio", skewed (1.0f, 10.0f, 2.5f), 1.5f, ":1");
        fl (gId, n + " Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    }
    fl ("mbAttack", "MB Attack", skewed (0.5f, 200.0f, 20.0f), 20.0f, "ms");
    fl ("mbRelease", "MB Release", skewed (10.0f, 2000.0f, 200.0f), 160.0f, "ms");

    bo ("compOn", "Glue On", true);
    fl ("compThresh", "Glue Threshold", { -48.0f, 0.0f, 0.01f }, -18.0f, "dB");
    fl ("compRatio", "Glue Ratio", skewed (1.0f, 10.0f, 2.5f), 2.0f, ":1");
    fl ("compAttack", "Glue Attack", skewed (0.1f, 100.0f, 10.0f), 30.0f, "ms");
    fl ("compRelease", "Glue Release", skewed (10.0f, 2000.0f, 250.0f), 250.0f, "ms");
    fl ("compKnee", "Glue Knee", { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
    fl ("compMakeup", "Glue Makeup", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    fl ("compMix", "Glue Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");
    fl ("compScHpf", "Glue SC HPF", freqRange (10.0f, 400.0f), 90.0f, "Hz");

    bo ("satOn", "Saturate On", false);
    fl ("satDrive", "Saturate Drive", { 0.0f, 18.0f, 0.01f }, 3.0f, "dB");
    fl ("satWarmth", "Saturate Warmth", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    fl ("satMix", "Saturate Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");

    bo ("limOn", "Limiter On", true);
    fl ("limGain", "Limiter Drive", { 0.0f, 24.0f, 0.01f }, 0.0f, "dB");
    fl ("limCeiling", "Ceiling", { -12.0f, 0.0f, 0.01f }, -1.0f, "dBTP");
    fl ("limRelease", "Limiter Release", skewed (1.0f, 1000.0f, 80.0f), 80.0f, "ms");
    ch ("lookahead", "Lookahead", lookaheadChoices, 2);
    bo ("autoRelease", "Auto Release", true);
    bo ("truePeak", "True Peak", true);
    ch ("dither", "Dither", ditherChoices, 0);

    fl ("targetLufs", "Target Loudness", { -24.0f, -5.0f, 0.1f }, -14.0f, "LUFS");
    return layout;
}

// Copies the current parameter values into the DSP parameter struct once per
// block. Every pointer is looked up once, in the constructor, so the audio thread
// never allocates or does a string lookup.
class ParamReader
{
public:
    explicit ParamReader (APVTS& s)
    {
        auto r = [&s] (const juce::String& id)
        {
            auto* p = s.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        };
        inGain = r ("inGain");
        eqOn = r ("eqOn"); lowCutOn = r ("lowCutOn"); lowCutHz = r ("lowCutHz");
        lowShelfHz = r ("lowShelfHz"); lowShelfDb = r ("lowShelfDb");
        lowMidHz = r ("lowMidHz"); lowMidDb = r ("lowMidDb"); lowMidQ = r ("lowMidQ");
        highMidHz = r ("highMidHz"); highMidDb = r ("highMidDb"); highMidQ = r ("highMidQ");
        highShelfHz = r ("highShelfHz"); highShelfDb = r ("highShelfDb");
        stereoOn = r ("stereoOn"); width = r ("width"); monoBassOn = r ("monoBassOn"); monoBassHz = r ("monoBassHz");
        mbOn = r ("mbOn"); xoverLow = r ("xoverLow"); xoverHigh = r ("xoverHigh");
        for (int b = 0; b < 3; ++b)
        {
            bandThresh[(size_t) b] = r ("band" + juce::String (b) + "Thresh");
            bandRatio[(size_t) b] = r ("band" + juce::String (b) + "Ratio");
            bandGain[(size_t) b] = r ("band" + juce::String (b) + "Gain");
        }
        mbAttack = r ("mbAttack"); mbRelease = r ("mbRelease");
        compOn = r ("compOn"); compThresh = r ("compThresh"); compRatio = r ("compRatio");
        compAttack = r ("compAttack"); compRelease = r ("compRelease"); compKnee = r ("compKnee");
        compMakeup = r ("compMakeup"); compMix = r ("compMix"); compScHpf = r ("compScHpf");
        satOn = r ("satOn"); satDrive = r ("satDrive"); satWarmth = r ("satWarmth"); satMix = r ("satMix");
        limOn = r ("limOn"); limGain = r ("limGain"); limCeiling = r ("limCeiling"); limRelease = r ("limRelease");
        lookahead = r ("lookahead"); autoRelease = r ("autoRelease"); truePeak = r ("truePeak"); dither = r ("dither");
    }

    void read (mb::ChainParams& p) const noexcept
    {
        auto g = [] (const std::atomic<float>* a) { return (double) a->load (std::memory_order_relaxed); };
        auto b = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) > 0.5f; };
        p.inputGainDb = g (inGain);
        p.eqOn = b (eqOn); p.lowCutOn = b (lowCutOn); p.lowCutHz = g (lowCutHz);
        p.lowShelfHz = g (lowShelfHz); p.lowShelfDb = g (lowShelfDb);
        p.lowMidHz = g (lowMidHz); p.lowMidDb = g (lowMidDb); p.lowMidQ = g (lowMidQ);
        p.highMidHz = g (highMidHz); p.highMidDb = g (highMidDb); p.highMidQ = g (highMidQ);
        p.highShelfHz = g (highShelfHz); p.highShelfDb = g (highShelfDb);
        p.stereoOn = b (stereoOn); p.width = g (width) * 0.01; p.monoBassOn = b (monoBassOn); p.monoBassHz = g (monoBassHz);
        p.mbOn = b (mbOn); p.xoverLowHz = g (xoverLow); p.xoverHighHz = g (xoverHigh);
        for (size_t i = 0; i < 3; ++i)
        {
            p.bandThreshDb[i] = g (bandThresh[i]);
            p.bandRatio[i] = g (bandRatio[i]);
            p.bandGainDb[i] = g (bandGain[i]);
        }
        p.mbAttackMs = g (mbAttack); p.mbReleaseMs = g (mbRelease);
        p.compOn = b (compOn); p.compThreshDb = g (compThresh); p.compRatio = g (compRatio);
        p.compAttackMs = g (compAttack); p.compReleaseMs = g (compRelease); p.compKneeDb = g (compKnee);
        p.compMakeupDb = g (compMakeup); p.compMix = g (compMix) * 0.01; p.compScHpfHz = g (compScHpf);
        p.satOn = b (satOn); p.satDriveDb = g (satDrive); p.satWarmth = g (satWarmth) * 0.01; p.satMix = g (satMix) * 0.01;
        p.limOn = b (limOn); p.limGainDb = g (limGain); p.limCeilingDb = g (limCeiling); p.limReleaseMs = g (limRelease);
        p.lookaheadMs = lookaheadMs[juce::jlimit (0, 4, (int) std::lround (g (lookahead)))];
        p.autoRelease = b (autoRelease); p.truePeak = b (truePeak);
        p.dither = juce::jlimit (0, 3, (int) std::lround (g (dither)));
    }

private:
    using P = std::atomic<float>*;
    P inGain, eqOn, lowCutOn, lowCutHz, lowShelfHz, lowShelfDb, lowMidHz, lowMidDb, lowMidQ, highMidHz, highMidDb, highMidQ,
      highShelfHz, highShelfDb, stereoOn, width, monoBassOn, monoBassHz, mbOn, xoverLow, xoverHigh, mbAttack, mbRelease,
      compOn, compThresh, compRatio, compAttack, compRelease, compKnee, compMakeup, compMix, compScHpf, satOn, satDrive,
      satWarmth, satMix, limOn, limGain, limCeiling, limRelease, lookahead, autoRelease, truePeak, dither;
    std::array<P, 3> bandThresh {}, bandRatio {}, bandGain {};
};
} // namespace mbp
