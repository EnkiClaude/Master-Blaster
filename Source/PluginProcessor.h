#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    mbp::APVTS state;

    // Meter values for the editor (written on the audio thread, read on the message thread).
    struct Meters
    {
        std::atomic<float> inPeak { -150.0f }, outPeak { -150.0f }, truePeakMax { -150.0f };
        std::atomic<float> compGr { 0.0f }, limGr { 0.0f }, bandGr[3] { { 0.0f }, { 0.0f }, { 0.0f } };
        std::atomic<float> momentary { -150.0f }, shortTerm { -150.0f }, integrated { -150.0f }, correlation { 1.0f };
    } meters;

    void requestLoudnessReset() { resetLoudnessFlag.store (true); }
    double getLatencyMs() const { return currentSampleRate > 0 ? 1000.0 * getLatencySamples() / currentSampleRate : 0.0; }

private:
    template <typename T> void process (juce::AudioBuffer<T>& buffer);

    mbp::ParamReader reader;
    mb::MasterChain chain;
    mb::ChainParams params;
    juce::AudioBuffer<double> scratch;
    std::atomic<bool> resetLoudnessFlag { false };
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterBlasterProcessor)
};
