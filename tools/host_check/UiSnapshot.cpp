// Renders the plugin editor offscreen to a PNG (used for docs and UI checks).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    MasterBlasterProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rnd (1);
    float lp[2] {};
    int sampleIndex = 0;
    auto feed = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                // Pink-ish noise plus a decaying 55 Hz "kick": a rough stand-in for music
                const int n = sampleIndex + i;
                const float kick = 0.5f * std::sin (2.0f * 3.14159265f * 55.0f * (float) n / 48000.0f) * std::exp (-(float) (n % 24000) / 4000.0f);
                for (int ch = 0; ch < 2; ++ch)
                {
                    lp[ch] = 0.96f * lp[ch] + 0.04f * (rnd.nextFloat() * 2.0f - 1.0f);
                    buf.setSample (ch, i, 1.5f * lp[ch] + kick);
                }
            }
            sampleIndex += 512;
            proc.processBlock (buf, midi);
        }
    };
    feed (400); // about 4 s, so the meters show something

    // Processor-level checks of presets, snapshots and undo
    int failures = 0;
    auto check = [&failures] (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what); if (! ok) ++failures; };
    auto value = [&proc] (const char* id) { return proc.state.getRawParameterValue (id)->load(); };
    auto setValue = [&proc] (const char* id, float v)
    {
        auto* p = proc.state.getParameter (id);
        p->setValueNotifyingHost (proc.state.getParameterRange (id).convertTo0to1 (v));
    };

    proc.setCurrentProgram (3); // Loud EDM / Club
    check (std::abs (value ("targetLufs") + 8.0f) < 0.01f && value ("mbOn") > 0.5f && value ("satOn") > 0.5f, "factory preset applies its values");
    proc.setCurrentProgram (0);
    check (std::abs (value ("targetLufs") + 14.0f) < 0.01f && value ("mbOn") < 0.5f, "Default preset resets everything");

    setValue ("limGain", 6.0f);
    proc.selectSnapshot (1);           // B starts as a copy of A
    setValue ("limGain", 3.0f);
    proc.selectSnapshot (0);           // back to A
    check (std::abs (value ("limGain") - 6.0f) < 0.01f, "snapshot A recalls its drive");
    proc.selectSnapshot (1);
    check (std::abs (value ("limGain") - 3.0f) < 0.01f, "snapshot B recalls its drive");

    // APVTS copies parameter values into its (undoable) state on a timer, so let the message loop run.
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };
    pump();
    proc.undoManager.beginNewTransaction();
    setValue ("compRatio", 4.0f);
    pump();
    proc.undoManager.beginNewTransaction();
    proc.undoManager.undo();
    pump();
    check (std::abs (value ("compRatio") - 4.0f) > 0.01f, "undo restores the previous value");

    setValue ("highShelfDb", 2.5f);
    check (proc.saveUserPreset ("__mb_test__"), "user preset saved");
    setValue ("highShelfDb", 0.0f);
    const auto file = MasterBlasterProcessor::userPresetFolder().getChildFile ("__mb_test__.mbpreset");
    check (proc.loadUserPreset (file) && std::abs (value ("highShelfDb") - 2.5f) < 0.01f, "user preset loads its values");
    file.deleteFile();

    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    MasterBlasterProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    check (proc2.getActiveSnapshot() == 1 && proc2.snapshotUsed (0), "snapshots survive a state save/restore");
    proc.setCurrentProgram (0);
    feed (200);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    // Reference track: write 20 s of pink-ish noise to a WAV and analyse it.
    {
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mb_ref_test.wav");
        wav.deleteFile();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), 44100.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> b (2, 44100);
            juce::Random rnd (3);
            float lp = 0.0f;
            for (int sec = 0; sec < 20; ++sec)
            {
                for (int i = 0; i < 44100; ++i)
                {
                    lp = 0.97f * lp + 0.03f * (rnd.nextFloat() * 2.0f - 1.0f);
                    b.setSample (0, i, lp * 2.0f); b.setSample (1, i, lp * 2.0f);
                }
                w->writeFromAudioSampleBuffer (b, 0, 44100);
            }
        }
        if (auto* mbEd = dynamic_cast<MasterBlasterEditor*> (ed.get()))
        {
            mbEd->getAnalyzer().computeReference (wav);
            for (int i = 0; i < 100 && ! mbEd->getAnalyzer().hasReference(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            check (mbEd->getAnalyzer().hasReference(), "reference track analysed");
        }
        wav.deleteFile();
    }
    // Keep audio flowing while the editor runs so the analyzer and meters fill in.
    for (int i = 0; i < 60; ++i)
    {
        feed (8);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out { juce::String (argc > 1 ? argv[1] : "ui.png") };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf (failures ? "%d FAILURE(S)\n" : "PROCESSOR CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
