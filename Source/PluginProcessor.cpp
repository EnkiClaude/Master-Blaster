#include "PluginProcessor.h"
#include "PluginEditor.h"

MasterBlasterProcessor::MasterBlasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "MasterBlaster", mbp::createLayout()),
      reader (state)
{
    reader.read (params);
    chain.setParams (params);
}

bool MasterBlasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MasterBlasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    reader.read (params);
    chain.setParams (params);
    chain.prepare (sampleRate);
    scratch.setSize (2, juce::jmax (512, samplesPerBlock), false, true, false);
    setLatencySamples (chain.getLatencySamples());
}

template <typename T>
void MasterBlasterProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    for (int ch = numCh; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh == 0) return;

    if (resetLoudnessFlag.exchange (false)) chain.resetLoudness();

    reader.read (params);
    chain.setParams (params);
    if (chain.applyLookaheadIfChanged()) setLatencySamples (chain.getLatencySamples());

    const int n = buffer.getNumSamples();
    if constexpr (std::is_same_v<T, double>)
    {
        double* L = buffer.getWritePointer (0);
        double* R = numCh > 1 ? buffer.getWritePointer (1) : L;
        chain.process (L, R, n);
    }
    else
    {
        // Single-precision host: convert to double and process in scratch-sized chunks
        // (no allocation, even if the host sends a block bigger than promised).
        const int cap = scratch.getNumSamples();
        for (int start = 0; start < n; start += cap)
        {
            const int len = juce::jmin (cap, n - start);
            double* L = scratch.getWritePointer (0);
            double* R = scratch.getWritePointer (1);
            const float* inL = buffer.getReadPointer (0, start);
            const float* inR = buffer.getReadPointer (numCh > 1 ? 1 : 0, start);
            for (int i = 0; i < len; ++i) { L[i] = inL[i]; R[i] = inR[i]; }
            chain.process (L, numCh > 1 ? R : L, len);
            float* outL = buffer.getWritePointer (0, start);
            for (int i = 0; i < len; ++i) outL[i] = (float) L[i];
            if (numCh > 1)
            {
                float* outR = buffer.getWritePointer (1, start);
                for (int i = 0; i < len; ++i) outR[i] = (float) R[i];
            }
        }
    }

    const auto& m = chain.getMeters();
    meters.inPeak.store ((float) m.inPeakDb);
    meters.outPeak.store ((float) m.outPeakDb);
    meters.truePeakMax.store ((float) m.truePeakMaxDb);
    meters.compGr.store ((float) m.compGrDb);
    meters.limGr.store ((float) m.limGrDb);
    for (int b = 0; b < 3; ++b) meters.bandGr[b].store ((float) m.bandGrDb[(size_t) b]);
    meters.momentary.store ((float) m.momentary);
    meters.shortTerm.store ((float) m.shortTerm);
    meters.integrated.store ((float) m.integrated);
    meters.correlation.store ((float) m.correlation);
}

void MasterBlasterProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process (b); }
void MasterBlasterProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

juce::AudioProcessorEditor* MasterBlasterProcessor::createEditor() { return new MasterBlasterEditor (*this); }

void MasterBlasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void MasterBlasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterBlasterProcessor(); }
