#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"

class MasterBlasterProcessor : public juce::AudioProcessor
{
public:
    MasterBlasterProcessor();
    ~MasterBlasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets are exposed to the host as programs.
    int getNumPrograms() override { return (int) mbp::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override
    {
        return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (mbp::factoryPresets()[(size_t) index].name) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::UndoManager undoManager { 30000, 30 };
    mbp::APVTS state;

    // ---- Presets ---------------------------------------------------------
    void loadFactoryPreset (int index);
    static juce::File userPresetFolder();
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String currentPresetName { "Default" };

    // ---- A/B/C/D snapshots ---------------------------------------------
    // Selecting a slot stores the current settings in the active slot and
    // recalls the selected one. An empty slot starts as a copy of the current settings.
    void selectSnapshot (int slot);
    int getActiveSnapshot() const { return activeSnapshot; }
    bool snapshotUsed (int slot) const { return snapshots[(size_t) slot].isValid(); }

    // ---- Compare (monitoring only; not saved, not automatable) ----------
    enum Compare { compareOff = 0, compareBypass, compareDelta };
    std::atomic<int> compare { compareOff };

    // ---- Analyzer feed: mono output samples, lock-free single producer ---
    static constexpr int analyzerSize = 1 << 15;
    void readAnalyzer (float* dest, int num) const; // the most recent `num` samples
    double getCurrentSampleRate() const { return currentSampleRate; }

    // Meter values for the editor (written on the audio thread, read on the message thread).
    struct Meters
    {
        std::atomic<float> inPeak { -150.0f }, outPeak { -150.0f }, truePeakMax { -150.0f };
        std::atomic<float> compGr { 0.0f }, limGr { 0.0f }, bandGr[3] { { 0.0f }, { 0.0f }, { 0.0f } };
        std::atomic<float> dynGr[2] { { 0.0f }, { 0.0f } }, autoMakeup { 0.0f }, matchGain { 0.0f };
        std::atomic<float> momentary { -150.0f }, shortTerm { -150.0f }, integrated { -150.0f }, correlation { 1.0f };
    } meters;

    void requestLoudnessReset() { resetLoudnessFlag.store (true); }
    double getLatencyMs() const { return currentSampleRate > 0 ? 1000.0 * getLatencySamples() / currentSampleRate : 0.0; }

private:
    template <typename T> void process (juce::AudioBuffer<T>& buffer);
    void applyValues (const std::map<juce::String, float>& values, bool resetOthers);
    std::map<juce::String, float> currentValues() const;
    juce::ValueTree snapshotsToTree() const;
    void snapshotsFromTree (const juce::ValueTree&);

    int currentProgram = 0;
    std::array<juce::ValueTree, 4> snapshots;
    int activeSnapshot = 0;
    std::vector<float> analyzerRing = std::vector<float> ((size_t) analyzerSize, 0.0f);
    std::atomic<uint32_t> analyzerWrite { 0 };

    mbp::ParamReader reader;
    mb::MasterChain chain;
    mb::ChainParams params;
    juce::AudioBuffer<double> scratch;
    std::atomic<bool> resetLoudnessFlag { false };
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterBlasterProcessor)
};
