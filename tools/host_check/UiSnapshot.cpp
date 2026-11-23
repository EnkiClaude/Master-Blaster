// Renders the plugin editor offscreen to a PNG (used for docs and UI checks).
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    MasterBlasterProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 400; ++b) // about 4 s of audio so the meters show something
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, 0.3f * std::sin (0.05f * (float) (b * 512 + i) * (ch ? 1.01f : 1.0f)));
        proc.processBlock (buf, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out { juce::String (argc > 1 ? argv[1] : "ui.png") };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    return 0;
}
