#include "PluginEditor.h"

using namespace juce;
namespace mbgui
{
//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::bg);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
    setColour (Label::textColourId, colours::dim);
    setColour (ComboBox::backgroundColourId, colours::bg);
    setColour (ComboBox::outlineColourId, colours::panelEdge);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::arrowColourId, colours::accent);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.3f));
    setColour (TextButton::buttonColourId, colours::panelEdge);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (ToggleButton::textColourId, colours::text);
}

void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = 3.5f;
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();
    const auto acc = (enabled ? colours::accent : colours::dim);

    Path track;
    track.addCentredArc (c.x, c.y, r - lineW, r - lineW, 0.0f, start, end, true);
    g.setColour (colours::panelEdge);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // Bipolar parameters (for example gains from -12 to +12) draw their arc from the centre.
    const auto range = s.getRange();
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    Path val;
    val.addCentredArc (c.x, c.y, r - lineW, r - lineW, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (acc);
    g.strokePath (val, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (colours::bg);
    g.fillEllipse (Rectangle<float> (r * 1.25f, r * 1.25f).withCentre (c));
    const Point<float> tip (c.x + std::sin (angle) * r * 0.55f, c.y - std::cos (angle) * r * 0.55f);
    g.setColour (colours::text);
    g.drawLine ({ c.getPointOnCircumference (r * 0.15f, angle), tip }, 2.0f);
}

void LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto area = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent.withAlpha (0.18f) : colours::bg);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (on ? colours::accent : (highlighted ? colours::dim : colours::panelEdge));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    g.setColour (on ? colours::text : colours::dim);
    g.setFont (FontOptions (12.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), Justification::centred, 1);
}

//==============================================================================
Knob::Knob (mbp::APVTS& s, const String& paramId, const String& text)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 72, 16);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, (double) s.getParameterRange (paramId).convertFrom0to1 (s.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (slider);
    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    addAndMakeVisible (label);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

Toggle::Toggle (mbp::APVTS& s, const String& paramId, const String& text)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, paramId, button);
}

Choice::Choice (mbp::APVTS& s, const String& paramId, const String& text)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    addAndMakeVisible (label);
    att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 24));
}

//==============================================================================
Panel::Panel (mbp::APVTS& s, const String& t, const String& powerParam, int cols) : state (s), title (t), columns (cols)
{
    if (powerParam.isNotEmpty())
    {
        power = std::make_unique<ToggleButton> ("ON");
        power->setClickingTogglesState (true);
        addAndMakeVisible (*power);
        powerAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, powerParam, *power);
    }
}

void Panel::addKnob (const String& id, const String& l) { items.push_back (std::make_unique<Knob> (state, id, l)); addAndMakeVisible (*items.back()); }
void Panel::addToggle (const String& id, const String& l) { items.push_back (std::make_unique<Toggle> (state, id, l)); addAndMakeVisible (*items.back()); }
void Panel::addChoice (const String& id, const String& l) { items.push_back (std::make_unique<Choice> (state, id, l)); addAndMakeVisible (*items.back()); }
void Panel::addSpacer() { items.push_back (std::make_unique<Component>()); }

int Panel::preferredHeight() const
{
    const int rows = ((int) items.size() + columns - 1) / columns;
    return titleH + rows * cellH + pad;
}

void Panel::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (title.toUpperCase(), Rectangle<int> (pad + 2, 0, getWidth(), titleH), Justification::centredLeft);
}

void Panel::resized()
{
    if (power) power->setBounds (getWidth() - pad - 40, 6, 40, 18);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int c = (int) i % columns, row = (int) i / columns;
        items[i]->setBounds (pad + c * cellW, titleH + row * cellH, cellW, cellH - 4);
    }
}

//==============================================================================
MeterPanel::MeterPanel (MasterBlasterProcessor& p) : proc (p), target (p.state, "targetLufs", "Target")
{
    addAndMakeVisible (target);
    addAndMakeVisible (matchButton);
    addAndMakeVisible (resetButton);
    matchButton.setTooltip ("Sets the limiter drive so the integrated loudness lands on the target. Play the loudest section first.");
    matchButton.onClick = [this] { matchLoudness(); };
    resetButton.onClick = [this] { proc.requestLoudnessReset(); status = "Integrated reset"; };
    startTimerHz (30);
}

void MeterPanel::matchLoudness()
{
    const float integrated = proc.meters.integrated.load();
    if (integrated < -69.0f) { status = "Play audio first"; return; }
    auto* drive = proc.state.getParameter ("limGain");
    const auto range = proc.state.getParameterRange ("limGain");
    const float current = range.convertFrom0to1 (drive->getValue());
    const float goal = proc.state.getRawParameterValue ("targetLufs")->load();
    const float next = jlimit (range.start, range.end, current + (goal - integrated));
    drive->beginChangeGesture();
    drive->setValueNotifyingHost (range.convertTo0to1 (next));
    drive->endChangeGesture();
    proc.requestLoudnessReset();
    status = String ("Drive ") + String (current, 1) + " -> " + String (next, 1) + " dB (press again to refine)";
}

void MeterPanel::timerCallback()
{
    auto& m = proc.meters;
    auto fall = [] (float& v, float t, float rate) { v = t > v ? t : jmax (t, v - rate); };
    fall (d.in, m.inPeak.load(), 1.5f);
    fall (d.out, m.outPeak.load(), 1.5f);
    d.tp = m.truePeakMax.load();
    fall (d.comp, m.compGr.load(), 0.6f);
    fall (d.lim, m.limGr.load(), 0.6f);
    for (int b = 0; b < 3; ++b) fall (d.band[b], m.bandGr[b].load(), 0.6f);
    d.m = m.momentary.load(); d.s = m.shortTerm.load(); d.i = m.integrated.load();
    d.corr = m.correlation.load();
    repaint();
}

static String fmtLufs (float v) { return v < -69.0f ? String ("--") : String (v, 1); }

void MeterPanel::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    auto area = getLocalBounds().reduced (10);
    g.setColour (colours::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText ("LOUDNESS", area.removeFromTop (20), Justification::centredLeft);

    // Large readouts
    auto row = area.removeFromTop (54);
    const float goal = proc.state.getRawParameterValue ("targetLufs")->load();
    auto readout = [&] (Rectangle<int> b, const String& name, const String& value, Colour c)
    {
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText (name, b.removeFromTop (14), Justification::centred);
        g.setColour (c);
        g.setFont (FontOptions (22.0f, Font::bold));
        g.drawText (value, b, Justification::centred);
    };
    const int w3 = row.getWidth() / 4;
    readout (row.removeFromLeft (w3), "MOMENTARY", fmtLufs (d.m), colours::text);
    readout (row.removeFromLeft (w3), "SHORT", fmtLufs (d.s), colours::text);
    const bool near = d.i > -69.0f && std::abs (d.i - goal) < 1.0f;
    readout (row.removeFromLeft (w3), "INTEGRATED", fmtLufs (d.i), near ? colours::accent : colours::accent2);
    readout (row, "TRUE PEAK", d.tp < -100.0f ? String ("--") : String (d.tp, 1),
             d.tp > proc.state.getRawParameterValue ("limCeiling")->load() + 0.05f ? colours::warn : colours::text);

    area.removeFromTop (100); // knob and buttons (positioned in resized)

    // Bar meters
    auto bar = [&] (const String& name, float value, float minV, float maxV, bool fromRight, Colour c)
    {
        auto b = area.removeFromTop (22);
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText (name, b.removeFromLeft (62), Justification::centredLeft);
        auto txt = b.removeFromRight (50);
        auto track = b.reduced (0, 6).toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (track, 3.0f);
        const float frac = jlimit (0.0f, 1.0f, (value - minV) / (maxV - minV));
        auto fill = fromRight ? track.withLeft (track.getRight() - track.getWidth() * frac) : track.withWidth (track.getWidth() * frac);
        g.setColour (c);
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (colours::text);
        g.drawText (fromRight ? (value < 0.05f ? String ("0.0") : String (-value, 1)) : (value < -100.0f ? String ("-inf") : String (value, 1)), txt, Justification::centredRight);
    };
    bar ("Input", d.in, -48.0f, 0.0f, false, colours::accent.withAlpha (0.8f));
    bar ("Output", d.out, -48.0f, 0.0f, false, colours::accent);
    area.removeFromTop (6);
    g.setColour (colours::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText ("GAIN REDUCTION", area.removeFromTop (20), Justification::centredLeft);
    bar ("MB Low", d.band[0], 0.0f, 12.0f, true, colours::accent2);
    bar ("MB Mid", d.band[1], 0.0f, 12.0f, true, colours::accent2);
    bar ("MB High", d.band[2], 0.0f, 12.0f, true, colours::accent2);
    bar ("Glue", d.comp, 0.0f, 12.0f, true, colours::accent2);
    bar ("Limiter", d.lim, 0.0f, 12.0f, true, colours::warn);
    area.removeFromTop (6);

    // Correlation (+1 mono, 0 wide, below 0 out of phase)
    {
        auto b = area.removeFromTop (22);
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText ("Correlation", b.removeFromLeft (62), Justification::centredLeft);
        auto txt = b.removeFromRight (50);
        auto track = b.reduced (0, 6).toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (track, 3.0f);
        const float x = track.getX() + track.getWidth() * (jlimit (-1.0f, 1.0f, d.corr) * 0.5f + 0.5f);
        g.setColour (d.corr < 0.0f ? colours::warn : colours::accent);
        g.fillRect (Rectangle<float> (x - 2.0f, track.getY() - 2.0f, 4.0f, track.getHeight() + 4.0f));
        g.setColour (colours::text);
        g.drawText (String (d.corr, 2), txt, Justification::centredRight);
    }

    g.setColour (colours::dim);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (status, area.removeFromTop (30), Justification::centredLeft, 2);
    g.drawText ("Latency " + String (proc.getLatencyMs(), 2) + " ms (" + String (proc.getLatencySamples()) + " samples)",
                getLocalBounds().reduced (10).removeFromBottom (16), Justification::centredLeft);
}

void MeterPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (20 + 54);
    auto row = area.removeFromTop (100);
    target.setBounds (row.removeFromLeft (Panel::cellW).withHeight (Panel::cellH));
    row.removeFromLeft (10);
    auto buttons = row.withSizeKeepingCentre (row.getWidth(), 64);
    matchButton.setBounds (buttons.removeFromTop (28));
    buttons.removeFromTop (8);
    resetButton.setBounds (buttons.removeFromTop (28));
}
} // namespace mbgui

//==============================================================================
MasterBlasterEditor::MasterBlasterEditor (MasterBlasterProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      eq (p.state, "Input / EQ", "eqOn", 8),
      stereo (p.state, "Stereo", "stereoOn", 3),
      sat (p.state, "Saturate", "satOn", 3),
      mb (p.state, "Multiband", "mbOn", 7),
      glue (p.state, "Glue Comp", "compOn", 4),
      lim (p.state, "Limiter", "limOn", 4),
      meters (p)
{
    // Row 1: frequencies and Qs; row 2: the matching gains underneath.
    eq.addKnob ("inGain", "Input");
    eq.addKnob ("lowCutHz", "Low Cut");
    eq.addKnob ("lowShelfHz", "Low Freq");
    eq.addKnob ("lowMidHz", "LM Freq");
    eq.addKnob ("lowMidQ", "LM Q");
    eq.addKnob ("highMidHz", "HM Freq");
    eq.addKnob ("highMidQ", "HM Q");
    eq.addKnob ("highShelfHz", "Air Freq");
    eq.addSpacer();
    eq.addToggle ("lowCutOn", "Low Cut");
    eq.addKnob ("lowShelfDb", "Low");
    eq.addKnob ("lowMidDb", "Low Mid");
    eq.addSpacer();
    eq.addKnob ("highMidDb", "High Mid");
    eq.addSpacer();
    eq.addKnob ("highShelfDb", "Air");

    stereo.addKnob ("width", "Width");
    stereo.addToggle ("monoBassOn", "Mono Bass");
    stereo.addKnob ("monoBassHz", "Mono Below");

    sat.addKnob ("satDrive", "Drive");
    sat.addKnob ("satWarmth", "Warmth");
    sat.addKnob ("satMix", "Mix");

    mb.addKnob ("xoverLow", "X-Low");
    mb.addKnob ("band0Thresh", "Low Thr");
    mb.addKnob ("band0Ratio", "Low Ratio");
    mb.addKnob ("band0Gain", "Low Gain");
    mb.addKnob ("band1Thresh", "Mid Thr");
    mb.addKnob ("band1Ratio", "Mid Ratio");
    mb.addKnob ("band1Gain", "Mid Gain");
    mb.addKnob ("xoverHigh", "X-High");
    mb.addKnob ("band2Thresh", "High Thr");
    mb.addKnob ("band2Ratio", "High Ratio");
    mb.addKnob ("band2Gain", "High Gain");
    mb.addKnob ("mbAttack", "Attack");
    mb.addKnob ("mbRelease", "Release");

    glue.addKnob ("compThresh", "Threshold");
    glue.addKnob ("compRatio", "Ratio");
    glue.addKnob ("compAttack", "Attack");
    glue.addKnob ("compRelease", "Release");
    glue.addKnob ("compKnee", "Knee");
    glue.addKnob ("compScHpf", "SC HPF");
    glue.addKnob ("compMakeup", "Makeup");
    glue.addKnob ("compMix", "Mix");

    lim.addKnob ("limGain", "Drive");
    lim.addKnob ("limCeiling", "Ceiling");
    lim.addKnob ("limRelease", "Release");
    lim.addChoice ("lookahead", "Lookahead");
    lim.addToggle ("autoRelease", "Auto Rel");
    lim.addToggle ("truePeak", "True Peak");
    lim.addChoice ("dither", "Dither");

    for (auto* c : std::initializer_list<Component*> { &eq, &stereo, &sat, &mb, &glue, &lim, &meters })
        content.addAndMakeVisible (c);
    addAndMakeVisible (content);
    setLookAndFeel (&lnf); // after the children exist, so they all pick it up

    // Grid: [EQ | Limiter] / [Multiband | Glue] / [Stereo | Saturate], meters on the right.
    const int gap = 8, header = 44;
    leftW = jmax (eq.preferredWidth() + gap + lim.preferredWidth(), mb.preferredWidth() + gap + glue.preferredWidth());
    row1H = jmax (eq.preferredHeight(), lim.preferredHeight());
    row2H = jmax (mb.preferredHeight(), glue.preferredHeight());
    row3H = jmax (stereo.preferredHeight(), sat.preferredHeight());
    baseW = gap + leftW + gap + 300 + gap;
    baseH = header + row1H + gap + row2H + gap + row3H + gap;

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / baseH);
    setSize (baseW, baseH);
}

MasterBlasterEditor::~MasterBlasterEditor() { setLookAndFeel (nullptr); }

void MasterBlasterEditor::paint (Graphics& g)
{
    g.fillAll (mbgui::colours::bg);
    const float scale = (float) getWidth() / (float) baseW;
    g.addTransform (AffineTransform::scale (scale));
    g.setColour (mbgui::colours::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("MASTER BLASTER", 12, 0, 400, 44, Justification::centredLeft);
    g.setColour (mbgui::colours::dim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("mastering chain  |  64-bit  |  true-peak", 230, 0, 400, 44, Justification::centredLeft);
}

void MasterBlasterEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (AffineTransform::scale (scale));

    const int gap = 8, header = 44;
    auto placeRow = [&] (mbgui::Panel& a, mbgui::Panel& b, int y, int h)
    {
        a.setBounds (gap, y, a.preferredWidth(), h);
        b.setBounds (a.getRight() + gap, y, gap + leftW - a.getRight() - gap, h);
    };
    int y = header;
    placeRow (eq, lim, y, row1H);   y += row1H + gap;
    placeRow (mb, glue, y, row2H);  y += row2H + gap;
    const int half = (leftW - gap) / 2;
    stereo.setBounds (gap, y, half, row3H);
    sat.setBounds (stereo.getRight() + gap, y, leftW - half - gap, row3H);
    meters.setBounds (gap + leftW + gap, header, baseW - leftW - 3 * gap, baseH - header - gap);
}
