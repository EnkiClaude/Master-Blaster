#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace mbgui
{
namespace colours
{
const juce::Colour bg { 0xff101317 }, panel { 0xff181c22 }, panelEdge { 0xff262c35 };
const juce::Colour text { 0xffdfe5ec }, dim { 0xff7d8794 }, accent { 0xff35d0ba }, accent2 { 0xffffb547 }, warn { 0xffff5a5f };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
};

// A labelled rotary knob attached to a parameter.
class Knob : public juce::Component
{
public:
    Knob (mbp::APVTS& s, const juce::String& paramId, const juce::String& label);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class Toggle : public juce::Component
{
public:
    Toggle (mbp::APVTS& s, const juce::String& paramId, const juce::String& label);
    void resized() override { button.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), 24)); }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class Choice : public juce::Component
{
public:
    Choice (mbp::APVTS& s, const juce::String& paramId, const juce::String& label);
    void resized() override;

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

// A module panel: a title bar with a power switch, then a grid of controls.
class Panel : public juce::Component
{
public:
    static constexpr int cellW = 74, cellH = 90, titleH = 30, pad = 8;

    Panel (mbp::APVTS& s, const juce::String& title, const juce::String& powerParam, int columns);
    void addKnob (const juce::String& id, const juce::String& label);
    void addToggle (const juce::String& id, const juce::String& label);
    void addChoice (const juce::String& id, const juce::String& label);
    void addSpacer();
    int preferredWidth() const { return columns * cellW + 2 * pad; }
    int preferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    mbp::APVTS& state;
    juce::String title;
    int columns;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    std::vector<std::unique_ptr<juce::Component>> items;
};

// Loudness, peak, correlation and gain-reduction readouts.
class MeterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MeterPanel (MasterBlasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void matchLoudness();

    MasterBlasterProcessor& proc;
    Knob target;
    juce::TextButton matchButton { "Match" }, resetButton { "Reset" };
    struct Disp { float in = -150, out = -150, tp = -150, comp = 0, lim = 0, band[3] {}, m = -150, s = -150, i = -150, corr = 1; } d;
    juce::String status;
};
} // namespace mbgui

class MasterBlasterEditor : public juce::AudioProcessorEditor
{
public:
    explicit MasterBlasterEditor (MasterBlasterProcessor&);
    ~MasterBlasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MasterBlasterProcessor& proc;
    mbgui::LookAndFeel lnf;
    juce::Component content;
    mbgui::Panel eq, stereo, sat, mb, glue, lim;
    mbgui::MeterPanel meters;
    int baseW = 0, baseH = 0, leftW = 0, row1H = 0, row2H = 0, row3H = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterBlasterEditor)
};
