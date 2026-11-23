#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// Factory presets: starting points, not finished masters. Any parameter a
// preset doesn't list is reset to its default.
namespace mbp
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Default", {} },

        { "Streaming -14 (transparent)",
          { { "compThresh", -20.0f }, { "compRatio", 1.5f }, { "compAttack", 30.0f }, { "compRelease", 200.0f },
            { "compAutoRel", 1.0f }, { "limRelease", 100.0f }, { "limCeiling", -1.0f }, { "targetLufs", -14.0f } } },

        { "Apple Music -16",
          { { "compThresh", -22.0f }, { "compRatio", 1.5f }, { "compAutoRel", 1.0f }, { "limRelease", 120.0f },
            { "limCeiling", -1.0f }, { "targetLufs", -16.0f } } },

        { "Loud EDM / Club",
          { { "lowCutHz", 28.0f }, { "lowShelfHz", 70.0f }, { "lowShelfDb", 1.5f }, { "highShelfHz", 12000.0f }, { "highShelfDb", 1.5f },
            { "width", 110.0f }, { "monoBassHz", 150.0f },
            { "mbOn", 1.0f }, { "band0Thresh", -18.0f }, { "band0Ratio", 2.0f }, { "band1Thresh", -20.0f }, { "band1Ratio", 1.5f },
            { "band2Thresh", -24.0f }, { "band2Ratio", 2.0f },
            { "compThresh", -18.0f }, { "compRatio", 2.0f }, { "compAttack", 10.0f }, { "compRelease", 150.0f }, { "compAutoRel", 1.0f },
            { "satOn", 1.0f }, { "satDrive", 4.0f }, { "satWarmth", 20.0f }, { "satMix", 60.0f },
            { "lookahead", 1.0f }, { "limRelease", 40.0f }, { "limCeiling", -1.0f }, { "targetLufs", -8.0f } } },

        { "Hip-Hop / Trap",
          { { "lowCutHz", 25.0f }, { "lowShelfHz", 60.0f }, { "lowShelfDb", 1.0f }, { "monoBassHz", 140.0f },
            { "mbOn", 1.0f }, { "band0Thresh", -16.0f }, { "band0Ratio", 2.5f }, { "band1Ratio", 1.2f }, { "band2Ratio", 1.2f },
            { "compThresh", -16.0f }, { "compRatio", 2.0f }, { "compAttack", 30.0f }, { "compRelease", 100.0f }, { "compAutoRel", 1.0f },
            { "satOn", 1.0f }, { "satDrive", 3.0f }, { "satWarmth", 30.0f }, { "satMix", 50.0f },
            { "limRelease", 60.0f }, { "targetLufs", -9.0f } } },

        { "Rock / Band",
          { { "lowCutHz", 30.0f }, { "lowMidHz", 300.0f }, { "lowMidDb", -1.0f }, { "highMidHz", 3000.0f }, { "highMidDb", 1.0f },
            { "highShelfDb", 1.0f },
            { "compThresh", -18.0f }, { "compRatio", 2.0f }, { "compAttack", 30.0f }, { "compRelease", 150.0f }, { "compAutoRel", 1.0f },
            { "satOn", 1.0f }, { "satDrive", 3.0f }, { "satMix", 40.0f }, { "targetLufs", -10.0f } } },

        { "Gentle Acoustic",
          { { "lowCutHz", 20.0f }, { "highShelfHz", 14000.0f }, { "highShelfDb", 1.0f }, { "monoBassHz", 80.0f },
            { "compThresh", -24.0f }, { "compRatio", 1.3f }, { "compAttack", 50.0f }, { "compRelease", 400.0f }, { "compKnee", 12.0f },
            { "lookahead", 4.0f }, { "limRelease", 200.0f }, { "targetLufs", -16.0f } } },

        { "Podcast / Voice -16",
          { { "lowCutHz", 70.0f }, { "lowMidHz", 250.0f }, { "lowMidDb", -2.0f }, { "lowMidQ", 1.0f },
            { "highMidHz", 3500.0f }, { "highMidDb", 2.0f }, { "highMidQ", 0.8f }, { "monoBassHz", 150.0f },
            { "dynOn", 1.0f }, { "dyn0Hz", 250.0f }, { "dyn0Q", 1.2f }, { "dyn0Thresh", -24.0f }, { "dyn0Range", -3.0f },
            { "dyn1Hz", 6500.0f }, { "dyn1Q", 3.0f }, { "dyn1Thresh", -30.0f }, { "dyn1Range", -6.0f },
            { "compThresh", -24.0f }, { "compRatio", 3.0f }, { "compAttack", 10.0f }, { "compRelease", 150.0f },
            { "compAutoRel", 1.0f }, { "compAutoMakeup", 1.0f }, { "limCeiling", -1.5f }, { "targetLufs", -16.0f } } },

        { "Broadcast EBU R128 -23",
          { { "compThresh", -26.0f }, { "compRatio", 1.5f }, { "compAutoRel", 1.0f }, { "limRelease", 150.0f },
            { "limCeiling", -1.0f }, { "targetLufs", -23.0f } } },

        { "De-harsh (dynamic EQ)",
          { { "dynOn", 1.0f }, { "dyn0Hz", 3200.0f }, { "dyn0Q", 2.0f }, { "dyn0Thresh", -26.0f }, { "dyn0Range", -4.0f },
            { "dyn1Hz", 7000.0f }, { "dyn1Q", 3.0f }, { "dyn1Thresh", -30.0f }, { "dyn1Range", -5.0f } } },

        { "Wide & Bright (M/S)",
          { { "eqMode", 2.0f }, { "highShelfHz", 8000.0f }, { "highShelfDb", 2.0f }, { "lowShelfHz", 200.0f }, { "lowShelfDb", -2.0f },
            { "width", 115.0f }, { "monoBassHz", 140.0f } } },
    };
    return presets;
}
} // namespace mbp
