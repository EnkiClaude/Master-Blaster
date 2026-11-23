// Loads the built VST3 the way a DAW would and checks it: processing at
// several sample rates and block sizes, latency, ceiling compliance,
// state round-trips, and a UI snapshot.
// Usage: mb_host_check <path/to/Master Blaster.vst3> [snapshot.png]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what, const String& detail = {})
{
    std::printf ("[%s] %s %s\n", ok ? " OK " : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
    if (! ok) ++failures;
}

static AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name) return prm;
    return nullptr;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: %s plugin.vst3 [snapshot.png]\n", argv[0]); return 2; }
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    ScopedJuceInitialiser_GUI gui;
    AudioPluginFormatManager fm;
    fm.addFormat (new VST3PluginFormat());

    OwnedArray<PluginDescription> types;
    for (auto* f : fm.getFormats()) f->findAllTypesForFile (types, String (CharPointer_UTF8 (argv[1])));
    check (types.size() == 1, "plugin scanned", types.isEmpty() ? String() : types[0]->name + " / " + types[0]->pluginFormatName);
    if (types.isEmpty()) return 1;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        String err;
        auto plugin = fm.createPluginInstance (*types[0], sr, 4096, err);
        check (plugin != nullptr, "instantiate @" + String (sr), err);
        if (! plugin) return 1;
        plugin->enableAllBuses();
        plugin->setPlayConfigDetails (2, 2, sr, 4096);
        plugin->prepareToPlay (sr, 4096);
        const int latency = plugin->getLatencySamples();
        check (latency > 0 && latency < sr * 0.01, "latency reported", String (latency) + " samples = " + String (1000.0 * latency / sr, 2) + " ms");

        if (auto* drive = findParam (*plugin, "Limiter Drive"))
            drive->setValueNotifyingHost (0.5f); // 12 dB of a 0..24 dB range
        else check (false, "find Limiter Drive parameter");

        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.25f);
        MidiBuffer midi;
        float peak = 0.0f; bool finite = true;
        const int sizes[] { 512, 1, 37, 256, 2048, 4096 }; // irregular sizes, up to the prepared maximum
        int done = 0, k = 0;
        while (done < (int) sr * 6)
        {
            const int n = sizes[k++ % 6];
            AudioBuffer<float> buf (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, nd (rng) + 0.5f * std::sin (0.02f * (float) (done + i)));
            plugin->processBlock (buf, midi);
            if (done > sr) // ignore the first second
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = buf.getSample (ch, i);
                        finite &= std::isfinite (v);
                        peak = jmax (peak, std::abs (v));
                    }
            done += n;
        }
        check (finite, "output finite with odd block sizes @" + String (sr));
        check (peak <= Decibels::decibelsToGain (-1.0f) * 1.0001f, "sample peak under -1 dB ceiling @" + String (sr),
               String (Decibels::gainToDecibels (peak), 3) + " dBFS");

        // State round-trip
        MemoryBlock mb;
        plugin->getStateInformation (mb);
        auto* ceil = findParam (*plugin, "Ceiling");
        const float before = ceil ? ceil->getValue() : 0.0f;
        if (ceil) ceil->setValueNotifyingHost (0.1f);
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
        check (ceil && std::abs (ceil->getValue() - before) < 1e-6f, "state save/restore round-trip");

        // Fuzzed state restore (as pluginval does): random values, save, re-randomise, restore, compare.
        {
            Random rnd (sr == 44100.0 ? 11 : 12);
            auto params = plugin->getParameters();
            // Like pluginval, let the message loop run after setting values: the plugin
            // snaps bool/choice values to whole steps on its own timer.
            auto pump = [] { MessageManager::getInstance()->runDispatchLoopUntil (150); };
            for (auto* prm : params) prm->setValueNotifyingHost (rnd.nextFloat());
            pump();
            std::vector<float> saved;
            for (auto* prm : params)
            {
                // The state stores whole steps for bool/choice parameters, so a restore yields the snapped value.
                float v = prm->getValue();
                const int steps = prm->getNumSteps();
                if (prm->isDiscrete() && steps > 1 && steps < 100) v = std::round (v * (float) (steps - 1)) / (float) (steps - 1);
                saved.push_back (v);
            }
            MemoryBlock st;
            plugin->getStateInformation (st);
            // Second pass: continuous parameters get new random values. Discrete ones move
            // off their step but stay inside it (saved "on" = 1.0 becomes 0.6), which is the
            // case pluginval caught: the stored value doesn't change, so a plain
            // replaceState() would leave the stray value in place.
            for (size_t i = 0; i < saved.size(); ++i)
            {
                auto* prm = params[(int) i];
                const int steps = prm->getNumSteps();
                if (prm->isDiscrete() && steps > 1 && steps < 100)
                    prm->setValueNotifyingHost (saved[i] + (saved[i] < 0.5f ? 1.0f : -1.0f) * 0.4f / (float) (steps - 1));
                else
                    prm->setValueNotifyingHost (rnd.nextFloat());
            }
            plugin->setStateInformation (st.getData(), (int) st.getSize());
            pump();
            int bad = 0;
            String which;
            for (size_t i = 0; i < saved.size(); ++i)
            {
                const auto name = params[(int) i]->getName (40);
                if (name == "Bypass" || name == "Program") continue; // added by the VST3 wrapper, not plugin state
                if (std::abs (params[(int) i]->getValue() - saved[i]) > 0.1f)
                {
                    ++bad;
                    which << name << " (saved " << String (saved[i], 3) << ", got " << String (params[(int) i]->getValue(), 3) << ") ";
                }
            }
            check (bad == 0, "fuzzed state restore @" + String (sr), which);
            AudioBuffer<float> buf (2, 512); buf.clear();
            plugin->processBlock (buf, midi);
            for (auto* prm : params) prm->setValueNotifyingHost (prm->getDefaultValue());
        }

        // A lookahead change must update the reported latency.
        if (auto* la = findParam (*plugin, "Lookahead"))
        {
            la->setValueNotifyingHost (1.0f); // 5 ms
            AudioBuffer<float> buf (2, 512); buf.clear();
            plugin->processBlock (buf, midi);
            check (plugin->getLatencySamples() > latency, "latency follows lookahead", String (plugin->getLatencySamples()));
        }

        if (sr == 48000.0 && argc > 2)
        {
            std::unique_ptr<AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
            check (ed != nullptr, "editor created", ed ? String (ed->getWidth()) + "x" + String (ed->getHeight()) : String());
            if (ed)
            {
                ed->setVisible (true);
                for (int i = 0; i < 10; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                File out { String (CharPointer_UTF8 (argv[2])) };
                out.deleteFile();
                FileOutputStream os (out);
                PNGImageFormat().writeImageToStream (img, os);
                check (os.getStatus().wasOk(), "editor snapshot written", out.getFullPathName());
            }
        }
        plugin->releaseResources();
    }
    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nHOST CHECK PASSED\n", failures);
    return failures ? 1 : 0;
}
