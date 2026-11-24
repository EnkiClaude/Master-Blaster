#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MasterChain.h"
#include <map>

namespace mbp
{
using APVTS = juce::AudioProcessorValueTreeState;

inline const juce::StringArray lookaheadChoices { "0.5 ms", "1 ms", "2 ms", "3 ms", "5 ms" };
inline constexpr double lookaheadMs[] { 0.5, 1.0, 2.0, 3.0, 5.0 };
inline const juce::StringArray ditherChoices { "Off", "24-bit TPDF", "16-bit TPDF", "16-bit shaped" };
inline const juce::StringArray msChoices { "Stereo", "Mid", "Side" };

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

// Text typed into a knob's value box: accepts "3.5k", "3.5 kHz", "-1.5 dB", "40%".
inline float parseValue (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    float v = t.getFloatValue();
    if (t.containsChar ('k')) v *= 1000.0f;
    return v;
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
                                                                 .withStringFromValueFunction (formatter (unit))
                                                                 .withValueFromStringFunction ([] (const String& t) { return parseValue (t); })));
    };
    auto bo = [&] (const String& id, const String& name, bool def)
    { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, v }, name, def)); };
    auto ch = [&] (const String& id, const String& name, const StringArray& c, int def)
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
    ch ("eqMode", "EQ Mode", msChoices, 0);

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
    ch ("mbMode", "MB Mode", msChoices, 0);

    bo ("dynOn", "Dynamic EQ On", false);
    const float dynHzDef[] { 250.0f, 3200.0f }, dynQDef[] { 1.5f, 2.0f };
    for (int b = 0; b < 2; ++b)
    {
        const String n ("Dyn " + String (b + 1) + " ");
        fl ("dyn" + String (b) + "Hz", n + "Freq", freqRange (40.0f, 16000.0f), dynHzDef[b], "Hz");
        fl ("dyn" + String (b) + "Q", n + "Q", skewed (0.3f, 8.0f, 1.5f), dynQDef[b], "");
        fl ("dyn" + String (b) + "Thresh", n + "Threshold", { -60.0f, 0.0f, 0.01f }, -24.0f, "dB");
        fl ("dyn" + String (b) + "Range", n + "Range", { -18.0f, 0.0f, 0.01f }, -4.0f, "dB");
    }
    fl ("dynAttack", "Dyn Attack", skewed (0.1f, 100.0f, 5.0f), 5.0f, "ms");
    fl ("dynRelease", "Dyn Release", skewed (10.0f, 1000.0f, 120.0f), 120.0f, "ms");

    bo ("compOn", "Glue On", true);
    fl ("compThresh", "Glue Threshold", { -48.0f, 0.0f, 0.01f }, -18.0f, "dB");
    fl ("compRatio", "Glue Ratio", skewed (1.0f, 10.0f, 2.5f), 2.0f, ":1");
    fl ("compAttack", "Glue Attack", skewed (0.1f, 100.0f, 10.0f), 30.0f, "ms");
    fl ("compRelease", "Glue Release", skewed (10.0f, 2000.0f, 250.0f), 250.0f, "ms");
    fl ("compKnee", "Glue Knee", { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
    fl ("compMakeup", "Glue Makeup", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    fl ("compMix", "Glue Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");
    fl ("compScHpf", "Glue SC HPF", freqRange (10.0f, 400.0f), 90.0f, "Hz");
    bo ("compAutoRel", "Glue Auto Release", false);
    bo ("compAutoMakeup", "Glue Auto Makeup", false);

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
        highShelfHz = r ("highShelfHz"); highShelfDb = r ("highShelfDb"); eqMode = r ("eqMode");
        mbMode = r ("mbMode"); dynOn = r ("dynOn"); dynAttack = r ("dynAttack"); dynRelease = r ("dynRelease");
        for (int b = 0; b < 2; ++b)
        {
            dynHz[(size_t) b] = r ("dyn" + juce::String (b) + "Hz");
            dynQ[(size_t) b] = r ("dyn" + juce::String (b) + "Q");
            dynThresh[(size_t) b] = r ("dyn" + juce::String (b) + "Thresh");
            dynRange[(size_t) b] = r ("dyn" + juce::String (b) + "Range");
        }
        compAutoRel = r ("compAutoRel"); compAutoMakeup = r ("compAutoMakeup");
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
        p.eqMode = juce::jlimit (0, 2, (int) std::lround (g (eqMode)));
        p.mbMode = juce::jlimit (0, 2, (int) std::lround (g (mbMode)));
        p.dynOn = b (dynOn); p.dynAttackMs = g (dynAttack); p.dynReleaseMs = g (dynRelease);
        for (size_t i = 0; i < 2; ++i)
        {
            p.dynHz[i] = g (dynHz[i]); p.dynQ[i] = g (dynQ[i]);
            p.dynThreshDb[i] = g (dynThresh[i]); p.dynRangeDb[i] = g (dynRange[i]);
        }
        p.compAutoRelease = b (compAutoRel); p.compAutoMakeup = b (compAutoMakeup);
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
    P eqMode, mbMode, dynOn, dynAttack, dynRelease, compAutoRel, compAutoMakeup;
    std::array<P, 2> dynHz {}, dynQ {}, dynThresh {}, dynRange {};
};
// Tooltip text for every control.
inline juce::String tooltipFor (const juce::String& id)
{
    static const std::map<juce::String, juce::String> tips {
        { "inGain", "Input trim before all processing." },
        { "eqOn", "EQ on/off (click-free crossfade)." },
        { "lowCutOn", "24 dB/oct low cut that removes sub-sonic rumble eating limiter headroom." },
        { "lowCutHz", "Low cut corner frequency. 20-30 Hz is typical for music." },
        { "lowShelfHz", "Low shelf corner frequency." }, { "lowShelfDb", "Low shelf gain. Keep moves small (+/-1-2 dB)." },
        { "lowMidHz", "Low-mid bell frequency (mud lives around 200-400 Hz)." }, { "lowMidDb", "Low-mid bell gain." },
        { "lowMidQ", "Low-mid bell width. Lower Q = broader." },
        { "highMidHz", "High-mid bell frequency (presence 2-5 kHz)." }, { "highMidDb", "High-mid bell gain." },
        { "highMidQ", "High-mid bell width." },
        { "highShelfHz", "Air shelf corner frequency." }, { "highShelfDb", "Air shelf gain. Matched-response design: accurate right up to 20 kHz." },
        { "eqMode", "Apply the four EQ bands to both channels (Stereo), the centre only (Mid) or the sides only (Side). The low cut is always stereo." },
        { "stereoOn", "Stereo section on/off." }, { "width", "Side-channel level. 100 % = unchanged, 0 % = mono." },
        { "monoBassOn", "Make everything below the Mono Below frequency mono (phase-coherent LR4 split)." },
        { "monoBassHz", "Below this frequency the signal is made mono." },
        { "mbOn", "Three-band compressor on/off." }, { "mbMode", "Compress both channels linked (Stereo), only the centre (Mid) or only the sides (Side)." },
        { "xoverLow", "Low/mid crossover frequency." }, { "xoverHigh", "Mid/high crossover frequency." },
        { "mbAttack", "Multiband attack time." }, { "mbRelease", "Multiband release time." },
        { "dynOn", "Dynamic EQ on/off: bells that cut only while their band gets loud." },
        { "dynAttack", "How fast the dynamic bands react." }, { "dynRelease", "How fast the dynamic bands let go." },
        { "compOn", "Glue compressor on/off." }, { "compThresh", "Level where compression starts. Aim for 1-3 dB of gain reduction." },
        { "compRatio", "Compression ratio. 1.5-2:1 is typical glue." }, { "compAttack", "Attack. Slower (30 ms+) lets transients through." },
        { "compRelease", "Release. With Auto Release on this is the base time." }, { "compKnee", "Soft-knee width." },
        { "compMakeup", "Manual makeup gain." }, { "compMix", "Parallel mix of compressed and dry signal." },
        { "compScHpf", "Sidechain high-pass: stops the kick and bass from pumping the whole mix." },
        { "compAutoRel", "Program-dependent release: fast after transients, slow on sustained material." },
        { "compAutoMakeup", "Adds back the average gain reduction so loudness stays the same while you compress." },
        { "satOn", "Saturation on/off." }, { "satDrive", "Saturation drive (output auto-compensated)." },
        { "satWarmth", "Adds even harmonics (asymmetric curve)." }, { "satMix", "Parallel saturation mix." },
        { "limOn", "Limiter on/off." }, { "limGain", "Drives the mix into the limiter. This is your loudness control." },
        { "limCeiling", "Maximum true-peak output level. -1 dBTP is the streaming recommendation." },
        { "limRelease", "Limiter release (base time when Auto Release is on)." },
        { "lookahead", "Limiter lookahead. Longer is smoother on transients but adds latency." },
        { "autoRelease", "Program-dependent limiter release." },
        { "truePeak", "Detect inter-sample (true) peaks with 16x interpolation." },
        { "dither", "Only when rendering to 16-bit (or 24-bit fixed point) and your DAW doesn't dither." },
        { "targetLufs", "Integrated loudness target for Auto Match. Streaming: -14, Apple: -16, broadcast: -23." },
    };
    if (auto it = tips.find (id); it != tips.end()) return it->second;
    if (id.startsWith ("band"))
    {
        if (id.endsWith ("Thresh")) return "Band threshold.";
        if (id.endsWith ("Ratio")) return "Band ratio.";
        if (id.endsWith ("Gain")) return "Band output gain (also works as a broad tone control).";
    }
    if (id.startsWith ("dyn"))
    {
        if (id.endsWith ("Hz")) return "Dynamic band centre frequency.";
        if (id.endsWith ("Q")) return "Dynamic band width.";
        if (id.endsWith ("Thresh")) return "The band starts cutting when its level passes this threshold.";
        if (id.endsWith ("Range")) return "The most this band may cut.";
    }
    return {};
}
} // namespace mbp
