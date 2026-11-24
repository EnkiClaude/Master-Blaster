#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace mbgui
{
namespace colours
{
const juce::Colour bg { 0xff101317 }, panel { 0xff181c22 }, panelEdge { 0xff262c35 };
const juce::Colour text { 0xffdfe5ec }, dim { 0xff7d8794 }, accent { 0xff35d0ba }, accent2 { 0xffffb547 }, warn { 0xffff5a5f };
const juce::Colour ref { 0xffb48cff };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
};

// A labelled rotary knob attached to a parameter. Double-click resets it; click the value to type one.
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
    void resized() override { button.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth() - 4, 24)); }

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

// Small horizontal gain-reduction bars, used in a panel's title bar.
class GrStrip : public juce::Component, private juce::Timer
{
public:
    struct Item { juce::String name; std::function<float()> value; };
    explicit GrStrip (std::vector<Item> items, float maxDb = 12.0f);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::vector<Item> items;
    std::vector<float> shown;
    float maxDb;
};

// A module panel: a title bar with a power switch, then a grid of controls.
// When the module is switched off its controls are dimmed but stay editable.
class Panel : public juce::Component
{
public:
    static constexpr int cellW = 74, cellH = 90, titleH = 30, pad = 8;

    Panel (mbp::APVTS& s, const juce::String& title, const juce::String& powerParam, int columns);
    void addKnob (const juce::String& id, const juce::String& label);
    void addToggle (const juce::String& id, const juce::String& label);
    void addChoice (const juce::String& id, const juce::String& label);
    void addSpacer();
    void setHeaderComponent (std::unique_ptr<juce::Component> c, int width);
    int preferredWidth() const { return columns * cellW + 2 * pad; }
    int preferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateDim();
    mbp::APVTS& state;
    juce::String title;
    int columns;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    std::vector<std::unique_ptr<juce::Component>> items;
    std::unique_ptr<juce::Component> header;
    int headerW = 0;
};

// Long-term average spectrum of the output, an optional reference track's
// spectrum (loudness-aligned) and the EQ curve.
class Analyzer : public juce::Component, private juce::Timer
{
public:
    explicit Analyzer (MasterBlasterProcessor&);
    ~Analyzer() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void computeReference (juce::File);           // analyses a file in the background
    bool hasReference() const { return ! refCurve.empty(); }

private:
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, numPoints = 240;
    void timerCallback() override;
    void loadReference();
    std::vector<float> toCurve (const std::vector<float>& power, double sampleRate) const;
    float xForFreq (float f) const;
    float eqResponseDb (float f) const;

    MasterBlasterProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData, avgPower;
    std::vector<float> outCurve, refCurve;
    juce::String refName;
    juce::TextButton loadButton { "Load reference..." }, clearButton { "Clear" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::Rectangle<float> plot;
};

// Loudness, peak, correlation and gain-reduction readouts, with Auto Match.
class MeterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MeterPanel (MasterBlasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepAutoMatch();

    MasterBlasterProcessor& proc;
    Knob target;
    juce::TextButton matchButton { "Auto Match" }, resetButton { "Reset" };
    struct Disp { float in = -150, out = -150, tp = -150, comp = 0, lim = 0, clip = 0, makeup = 0, match = 0, m = -150, s = -150, i = -150, lra = 0, corr = 1; } d;
    std::array<float, MasterBlasterProcessor::historySize> histS {}, histM {};
    juce::Rectangle<int> historyArea;
    juce::String status;

    // Auto Match: measure, adjust the limiter drive, repeat until on target.
    bool matching = false;
    int matchIteration = 0;
    double matchListenStart = 0.0;
};
} // namespace mbgui

class MasterBlasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterBlasterEditor (MasterBlasterProcessor&);
    ~MasterBlasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    mbgui::Analyzer& getAnalyzer() { return analyzer; }

private:
    void timerCallback() override;
    void refreshPresetMenu();
    void presetChosen();
    void savePreset();
    void setCompare (int mode);
    void setMonitor (int mode);

    MasterBlasterProcessor& proc;
    mbgui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Component content;
    mbgui::Panel eq, stereo, sat, mb, dyn, glue, clip, lim;
    mbgui::Analyzer analyzer;
    mbgui::MeterPanel meters;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, undoButton { "Undo" }, redoButton { "Redo" };
    std::array<juce::TextButton, 4> snapButtons;
    juce::TextButton bypassButton { "Bypass" }, deltaButton { "Delta" }, monoButton { "Mono" }, sideButton { "Side" };
    juce::Array<juce::File> userPresetFiles;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    int baseW = 0, baseH = 0, leftW = 0, rowH = 0;
    static constexpr int headerH = 50, gap = 8, analyzerH = 230, metersW = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterBlasterEditor)
};
