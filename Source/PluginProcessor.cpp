#include "PluginProcessor.h"
#include "PluginEditor.h"

MasterBlasterProcessor::MasterBlasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, &undoManager, "MasterBlaster", mbp::createLayout()),
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
    params.compare = compare.load (std::memory_order_relaxed);
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

    // Analyzer: mono sum of the output
    {
        uint32_t w = analyzerWrite.load (std::memory_order_relaxed);
        const auto* oL = buffer.getReadPointer (0);
        const auto* oR = buffer.getReadPointer (numCh > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            analyzerRing[(size_t) (w & (analyzerSize - 1))] = (float) (0.5 * ((double) oL[i] + (double) oR[i]));
            ++w;
        }
        analyzerWrite.store (w, std::memory_order_release);
    }

    const auto& m = chain.getMeters();
    meters.inPeak.store ((float) m.inPeakDb);
    meters.outPeak.store ((float) m.outPeakDb);
    meters.truePeakMax.store ((float) m.truePeakMaxDb);
    meters.compGr.store ((float) m.compGrDb);
    meters.limGr.store ((float) m.limGrDb);
    for (int b = 0; b < 3; ++b) meters.bandGr[b].store ((float) m.bandGrDb[(size_t) b]);
    for (int b = 0; b < 2; ++b) meters.dynGr[b].store ((float) m.dynGrDb[(size_t) b]);
    meters.autoMakeup.store ((float) m.autoMakeupDb);
    meters.matchGain.store ((float) m.matchGainDb);
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
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName ("Snapshots"), nullptr);
    tree.appendChild (snapshotsToTree(), nullptr);
    tree.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, destData);
}

void MasterBlasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            snapshotsFromTree (tree.getChildWithName ("Snapshots"));
            currentPresetName = tree.getProperty ("presetName", "Default").toString();
            tree.removeChild (tree.getChildWithName ("Snapshots"), nullptr);
            state.replaceState (tree);
            undoManager.clearUndoHistory();
        }
}

//==============================================================================
std::map<juce::String, float> MasterBlasterProcessor::currentValues() const
{
    std::map<juce::String, float> values;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
    return values;
}

void MasterBlasterProcessor::applyValues (const std::map<juce::String, float>& values, bool resetOthers)
{
    undoManager.beginNewTransaction();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float norm;
            if (auto it = values.find (rp->getParameterID()); it != values.end()) norm = rp->convertTo0to1 (it->second);
            else if (resetOthers) norm = rp->getDefaultValue();
            else continue;
            if (std::abs (norm - rp->getValue()) > 1.0e-7f)
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (norm);
                rp->endChangeGesture();
            }
        }
}

void MasterBlasterProcessor::loadFactoryPreset (int index)
{
    const auto& list = mbp::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    std::map<juce::String, float> values;
    for (const auto& [id, v] : list[(size_t) index].values) values[id] = v;
    applyValues (values, true);
    currentProgram = index;
    currentPresetName = list[(size_t) index].name;
}

juce::File MasterBlasterProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Master Blaster").getChildFile ("Presets");
}

juce::Array<juce::File> MasterBlasterProcessor::getUserPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.mbpreset");
    files.sort();
    return files;
}

bool MasterBlasterProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    juce::XmlElement xml ("MasterBlasterPreset");
    xml.setAttribute ("name", name.trim());
    for (const auto& [id, v] : currentValues())
    {
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", id);
        e->setAttribute ("value", (double) v);
    }
    if (! xml.writeTo (folder.getChildFile (clean + ".mbpreset"))) return false;
    currentPresetName = name.trim();
    return true;
}

bool MasterBlasterProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MasterBlasterPreset")) return false;
    std::map<juce::String, float> values;
    for (auto* e : xml->getChildWithTagNameIterator ("Param"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    applyValues (values, true);
    currentPresetName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    return true;
}

//==============================================================================
static juce::ValueTree valuesToTree (const std::map<juce::String, float>& values)
{
    juce::ValueTree t ("Snapshot");
    for (const auto& [id, v] : values) t.setProperty (id, v, nullptr);
    return t;
}

static std::map<juce::String, float> treeToValues (const juce::ValueTree& t)
{
    std::map<juce::String, float> values;
    for (int i = 0; i < t.getNumProperties(); ++i)
    {
        const auto name = t.getPropertyName (i);
        values[name.toString()] = (float) t.getProperty (name);
    }
    return values;
}

void MasterBlasterProcessor::selectSnapshot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, 4) || slot == activeSnapshot) return;
    snapshots[(size_t) activeSnapshot] = valuesToTree (currentValues());
    if (snapshots[(size_t) slot].isValid()) applyValues (treeToValues (snapshots[(size_t) slot]), false);
    else snapshots[(size_t) slot] = valuesToTree (currentValues());
    activeSnapshot = slot;
}

juce::ValueTree MasterBlasterProcessor::snapshotsToTree() const
{
    juce::ValueTree t ("Snapshots");
    t.setProperty ("active", activeSnapshot, nullptr);
    for (int i = 0; i < 4; ++i)
    {
        // The active slot always holds the live settings.
        auto snap = i == activeSnapshot ? valuesToTree (currentValues()) : snapshots[(size_t) i];
        if (snap.isValid()) t.appendChild (snap.createCopy().setProperty ("slot", i, nullptr), nullptr);
    }
    return t;
}

void MasterBlasterProcessor::snapshotsFromTree (const juce::ValueTree& t)
{
    for (auto& s : snapshots) s = {};
    activeSnapshot = 0;
    if (! t.isValid()) return;
    activeSnapshot = juce::jlimit (0, 3, (int) t.getProperty ("active", 0));
    for (auto child : t)
    {
        const int slot = child.getProperty ("slot", -1);
        if (juce::isPositiveAndBelow (slot, 4))
        {
            auto copy = child.createCopy();
            copy.removeProperty ("slot", nullptr);
            snapshots[(size_t) slot] = copy;
        }
    }
}

void MasterBlasterProcessor::readAnalyzer (float* dest, int num) const
{
    num = juce::jmin (num, analyzerSize);
    const uint32_t w = analyzerWrite.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = analyzerRing[(size_t) ((w - (uint32_t) num + (uint32_t) i) & (analyzerSize - 1))];
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterBlasterProcessor(); }
