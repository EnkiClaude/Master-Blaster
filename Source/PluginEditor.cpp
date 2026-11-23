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
    setColour (TextButton::buttonOnColourId, colours::accent.withAlpha (0.35f));
    setColour (TextButton::textColourOnId, colours::text);
    setColour (TooltipWindow::backgroundColourId, colours::panel.brighter (0.15f));
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::panelEdge);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
    setColour (TextEditor::backgroundColourId, colours::bg);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, colours::panelEdge);
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
    const auto tip = mbp::tooltipFor (paramId);
    slider.setTooltip (tip + (tip.isEmpty() ? "" : "\n") + "Double-click: reset. Click the value to type one.");
    addAndMakeVisible (slider);
    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setInterceptsMouseClicks (false, false);
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
    button.setTooltip (mbp::tooltipFor (paramId));
    addAndMakeVisible (button);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, paramId, button);
}

Choice::Choice (mbp::APVTS& s, const String& paramId, const String& text)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    box.setTooltip (mbp::tooltipFor (paramId));
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
GrStrip::GrStrip (std::vector<Item> i, float maxDbIn) : items (std::move (i)), shown (items.size(), 0.0f), maxDb (maxDbIn)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void GrStrip::timerCallback()
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const float v = jmax (0.0f, items[i].value());
        shown[i] = v > shown[i] ? v : jmax (v, shown[i] - 0.5f);
    }
    repaint();
}

void GrStrip::paint (Graphics& g)
{
    const int n = (int) items.size();
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (3.0f, 0.0f);
        g.setColour (colours::dim);
        g.setFont (FontOptions (10.0f));
        g.drawText (items[(size_t) i].name, cell.removeFromLeft (22.0f), Justification::centredLeft);
        auto txt = cell.removeFromRight (26.0f);
        auto track = cell.withSizeKeepingCentre (cell.getWidth(), 6.0f);
        g.setColour (colours::bg);
        g.fillRoundedRectangle (track, 2.0f);
        const float frac = jlimit (0.0f, 1.0f, shown[(size_t) i] / maxDb);
        g.setColour (colours::accent2);
        g.fillRoundedRectangle (track.withLeft (track.getRight() - track.getWidth() * frac), 2.0f);
        g.setColour (colours::text);
        g.drawText (shown[(size_t) i] < 0.05f ? String ("0.0") : String (-shown[(size_t) i], 1), txt, Justification::centredRight);
    }
}

//==============================================================================
Panel::Panel (mbp::APVTS& s, const String& t, const String& powerParam, int cols) : state (s), title (t), columns (cols)
{
    if (powerParam.isNotEmpty())
    {
        power = std::make_unique<ToggleButton> ("ON");
        power->setClickingTogglesState (true);
        power->setTooltip (mbp::tooltipFor (powerParam));
        addAndMakeVisible (*power);
        powerAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, powerParam, *power);
        power->onStateChange = [this] { updateDim(); };
    }
}

void Panel::updateDim()
{
    const bool on = power == nullptr || power->getToggleState();
    for (auto& c : items) c->setAlpha (on ? 1.0f : 0.4f);
    if (header) header->setAlpha (on ? 1.0f : 0.4f);
}

void Panel::addKnob (const String& id, const String& l) { items.push_back (std::make_unique<Knob> (state, id, l)); addAndMakeVisible (*items.back()); updateDim(); }
void Panel::addToggle (const String& id, const String& l) { items.push_back (std::make_unique<Toggle> (state, id, l)); addAndMakeVisible (*items.back()); updateDim(); }
void Panel::addChoice (const String& id, const String& l) { items.push_back (std::make_unique<Choice> (state, id, l)); addAndMakeVisible (*items.back()); updateDim(); }
void Panel::addSpacer() { items.push_back (std::make_unique<Component>()); }
void Panel::setHeaderComponent (std::unique_ptr<Component> c, int width)
{
    header = std::move (c);
    headerW = width;
    addAndMakeVisible (*header);
    updateDim();
}

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
    const bool on = power == nullptr || power->getToggleState();
    g.setColour (on ? colours::text : colours::dim);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (title.toUpperCase(), Rectangle<int> (pad + 2, 0, getWidth(), titleH), Justification::centredLeft);
}

void Panel::resized()
{
    if (power) power->setBounds (getWidth() - pad - 40, 6, 40, 18);
    if (header) header->setBounds (getWidth() - pad - 48 - headerW, 5, headerW, 20);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int c = (int) i % columns, row = (int) i / columns;
        items[i]->setBounds (pad + c * cellW, titleH + row * cellH, cellW, cellH - 4);
    }
}

//==============================================================================
Analyzer::Analyzer (MasterBlasterProcessor& p) : proc (p)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    avgPower.assign ((size_t) fftSize / 2 + 1, 0.0f);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.setTooltip ("Load a reference track (WAV/AIFF/FLAC/MP3...). Its average spectrum is overlaid, loudness-aligned, so you can compare tonal balance.");
    clearButton.setTooltip ("Remove the reference curve.");
    loadButton.onClick = [this] { loadReference(); };
    clearButton.onClick = [this] { refCurve.clear(); refName.clear(); repaint(); };
    startTimerHz (25);
}

Analyzer::~Analyzer() { alive->store (false); }

void Analyzer::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (22);
    clearButton.setBounds (top.removeFromRight (60));
    top.removeFromRight (6);
    loadButton.setBounds (top.removeFromRight (140));
    r.removeFromTop (4);
    plot = r.toFloat().withTrimmedLeft (28.0f).withTrimmedBottom (14.0f);
}

float Analyzer::xForFreq (float f) const
{
    const float t = std::log (f / 20.0f) / std::log (20000.0f / 20.0f);
    return plot.getX() + t * plot.getWidth();
}

// Averages power into numPoints log-spaced 1/6-octave bands from 20 Hz to 20 kHz (in dB).
std::vector<float> Analyzer::toCurve (const std::vector<float>& power, double sampleRate) const
{
    std::vector<float> curve ((size_t) numPoints, -150.0f);
    const int bins = (int) power.size();
    const double binHz = sampleRate / (2.0 * (bins - 1));
    for (int i = 0; i < numPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / (numPoints - 1));
        const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        int b0 = jlimit (1, bins - 1, (int) std::floor (lo / binHz)), b1 = jlimit (b0, bins - 1, (int) std::ceil (hi / binHz));
        double sum = 0.0;
        for (int b = b0; b <= b1; ++b) sum += power[(size_t) b];
        const double avg = sum / (b1 - b0 + 1);
        // +3 dB/octave tilt display (pink-noise flat), the usual view for mastering
        curve[(size_t) i] = (float) (10.0 * std::log10 (avg + 1e-20) + 10.0 * std::log10 (f / 1000.0)); // +3.01 dB per octave
    }
    return curve;
}

void Analyzer::timerCallback()
{
    const double sr = proc.getCurrentSampleRate();
    if (sr <= 0.0) return;
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    proc.readAnalyzer (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    // Long-term average (about 2 s) so the curve shows tonal balance, not single notes.
    const float a = 0.02f;
    const float norm = 4.0f / (float) (fftSize * fftSize);
    for (size_t b = 0; b < avgPower.size(); ++b)
    {
        const float pw = fftData[b] * fftData[b] * norm;
        avgPower[b] += (pw - avgPower[b]) * a;
    }
    outCurve = toCurve (avgPower, sr);
    repaint();
}

float Analyzer::eqResponseDb (float f) const
{
    auto& s = proc.state;
    auto get = [&s] (const char* id) { return (double) s.getRawParameterValue (id)->load(); };
    if (get ("eqOn") < 0.5) return 0.0f;
    const double fs = jmax (44100.0, proc.getCurrentSampleRate());
    double db = 0.0;
    if (get ("lowCutOn") > 0.5)
    {
        const double r = get ("lowCutHz") / f;
        db += -10.0 * std::log10 (1.0 + std::pow (r, 8.0)); // 4th-order Butterworth
    }
    using T = mb::MatchedBiquad::Type;
    mb::MatchedBiquad b;
    b.setup (T::lowShelf, get ("lowShelfHz"), 0.7071, get ("lowShelfDb"), fs);      db += b.magnitudeDb (f, fs);
    b.setup (T::bell, get ("lowMidHz"), get ("lowMidQ"), get ("lowMidDb"), fs);      db += b.magnitudeDb (f, fs);
    b.setup (T::bell, get ("highMidHz"), get ("highMidQ"), get ("highMidDb"), fs);   db += b.magnitudeDb (f, fs);
    b.setup (T::highShelf, get ("highShelfHz"), 0.7071, get ("highShelfDb"), fs);   db += b.magnitudeDb (f, fs);
    return (float) db;
}

void Analyzer::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText ("ANALYZER", 10, 10, 120, 22, Justification::centredLeft);
    g.setFont (FontOptions (11.0f));
    // Legend
    {
        int x = 110;
        auto legend = [&] (Colour c, const String& t)
        {
            g.setColour (c); g.fillRect (x, 20, 14, 3); x += 18;
            g.setColour (colours::dim); g.drawText (t, x, 10, 200, 22, Justification::centredLeft);
            x += GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 16;
        };
        legend (colours::accent, "Output (avg, +3 dB/oct tilt)");
        legend (colours::text.withAlpha (0.8f), "EQ curve");
        if (refCurve.size() == (size_t) numPoints) legend (colours::ref, "Ref: " + refName);
    }

    // Grid
    const float top = plot.getY(), bottom = plot.getBottom();
    const float specTop = -20.0f, specRange = 60.0f; // spectrum dB range shown
    g.setFont (FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (colours::panelEdge);
        g.drawVerticalLine ((int) x, top, bottom);
        g.setColour (colours::dim);
        g.drawText (f >= 1000.0f ? String ((int) (f / 1000.0f)) + "k" : String ((int) f), (int) x - 15, (int) bottom, 30, 14, Justification::centred);
    }
    const float eqRange = 12.0f;
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = jmap (db, -eqRange, eqRange, bottom, top);
        g.setColour (db == 0.0f ? colours::dim.withAlpha (0.5f) : colours::panelEdge);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::dim);
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), (int) plot.getX() - 28, (int) y - 7, 24, 14, Justification::centredRight);
    }

    auto drawCurve = [&] (const std::vector<float>& c, Colour col, float thick, bool fill)
    {
        if (c.size() != (size_t) numPoints) return;
        Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (numPoints - 1);
            const float y = jlimit (top, bottom, jmap (c[(size_t) i], specTop - specRange, specTop, bottom, top));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (fill)
        {
            Path f (p);
            f.lineTo (plot.getRight(), bottom); f.lineTo (plot.getX(), bottom); f.closeSubPath();
            g.setColour (col.withAlpha (0.12f));
            g.fillPath (f);
        }
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick));
    };

    // Align the reference to the output: the same average level from 100 Hz to 10 kHz.
    if (refCurve.size() == (size_t) numPoints && outCurve.size() == (size_t) numPoints)
    {
        double so = 0, sr = 0; int n = 0;
        for (int i = 0; i < numPoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (numPoints - 1));
            if (f >= 100.0 && f <= 10000.0) { so += outCurve[(size_t) i]; sr += refCurve[(size_t) i]; ++n; }
        }
        const float shift = n > 0 ? (float) ((so - sr) / n) : 0.0f;
        std::vector<float> aligned (refCurve);
        for (auto& v : aligned) v += (outCurve[0] > -140.0f ? shift : 0.0f);
        drawCurve (aligned, colours::ref, 1.6f, false);
    }
    drawCurve (outCurve, colours::accent, 1.6f, true);

    // EQ curve (its own +/-12 dB scale)
    {
        Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / (numPoints - 1));
            const float x = plot.getX() + plot.getWidth() * (float) i / (numPoints - 1);
            const float y = jlimit (top, bottom, jmap (eqResponseDb (f), -eqRange, eqRange, bottom, top));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colours::text.withAlpha (0.8f));
        g.strokePath (p, PathStrokeType (1.5f));
    }
}

void Analyzer::loadReference()
{
    chooser = std::make_unique<FileChooser> ("Choose a reference track", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile()) computeReference (file);
                          });
}

void Analyzer::computeReference (File file)
{
    refName = file.getFileNameWithoutExtension() + " (analysing...)";
    repaint();
    auto aliveFlag = alive;
    SafePointer<Analyzer> safe (this);
    Thread::launch ([file, aliveFlag, safe]
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
        std::vector<float> power ((size_t) fftSize / 2 + 1, 0.0f);
        double sr = 44100.0;
        bool ok = false;
        if (reader != nullptr)
        {
            sr = reader->sampleRate;
            dsp::FFT f (fftOrder);
            dsp::WindowingFunction<float> w ((size_t) fftSize, dsp::WindowingFunction<float>::hann);
            AudioBuffer<float> buf ((int) jmax (1u, reader->numChannels), fftSize);
            std::vector<float> data ((size_t) fftSize * 2);
            const int64 total = std::min<int64> (reader->lengthInSamples, (int64) (sr * 600.0)); // first 10 minutes
            int frames = 0;
            for (int64 pos = 0; pos + fftSize <= total && aliveFlag->load(); pos += fftSize / 2)
            {
                reader->read (&buf, 0, fftSize, pos, true, true);
                std::fill (data.begin(), data.end(), 0.0f);
                for (int i = 0; i < fftSize; ++i)
                {
                    float sum = 0.0f;
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch) sum += buf.getSample (ch, i);
                    data[(size_t) i] = sum / (float) buf.getNumChannels();
                }
                w.multiplyWithWindowingTable (data.data(), (size_t) fftSize);
                f.performFrequencyOnlyForwardTransform (data.data(), true);
                const float norm = 4.0f / (float) (fftSize * fftSize);
                for (size_t b = 0; b < power.size(); ++b) power[b] += data[b] * data[b] * norm;
                ++frames;
            }
            if (frames > 0) { for (auto& p : power) p /= (float) frames; ok = true; }
        }
        MessageManager::callAsync ([safe, power, sr, ok, file]
        {
            if (auto* a = safe.getComponent())
            {
                if (ok) { a->refCurve = a->toCurve (power, sr); a->refName = file.getFileNameWithoutExtension(); }
                else { a->refCurve.clear(); a->refName = "could not read " + file.getFileName(); }
                a->repaint();
            }
        });
    });
}

//==============================================================================
MeterPanel::MeterPanel (MasterBlasterProcessor& p) : proc (p), target (p.state, "targetLufs", "Target")
{
    addAndMakeVisible (target);
    addAndMakeVisible (matchButton);
    addAndMakeVisible (resetButton);
    matchButton.setTooltip ("Play a representative (loud) section. Auto Match measures the integrated loudness, adjusts the limiter drive, "
                            "and repeats until the output sits on the target. Click again to cancel.");
    resetButton.setTooltip ("Reset the integrated loudness and true-peak max.");
    matchButton.setClickingTogglesState (false);
    matchButton.onClick = [this]
    {
        matching = ! matching;
        matchIteration = 0;
        if (matching) { proc.requestLoudnessReset(); matchListenStart = Time::getMillisecondCounterHiRes(); status = "Listening..."; }
        else status = "Auto Match cancelled";
        matchButton.setToggleState (matching, dontSendNotification);
    };
    resetButton.onClick = [this] { proc.requestLoudnessReset(); status = "Integrated reset"; };
    startTimerHz (30);
}

void MeterPanel::stepAutoMatch()
{
    constexpr double listenMs = 6000.0;
    const double elapsed = Time::getMillisecondCounterHiRes() - matchListenStart;
    const float integrated = proc.meters.integrated.load();
    if (elapsed < listenMs || integrated < -69.0f)
    {
        if (elapsed > 20000.0 && integrated < -69.0f) { matching = false; status = "Auto Match: no audio playing"; matchButton.setToggleState (false, dontSendNotification); }
        else status = "Listening... " + String (jmax (0.0, (listenMs - elapsed) / 1000.0), 1) + " s  (pass " + String (matchIteration + 1) + ")";
        return;
    }
    auto* drive = proc.state.getParameter ("limGain");
    const auto range = proc.state.getParameterRange ("limGain");
    const float current = range.convertFrom0to1 (drive->getValue());
    const float goal = proc.state.getRawParameterValue ("targetLufs")->load();
    const float err = goal - integrated;
    if (std::abs (err) < 0.3f || matchIteration >= 5)
    {
        matching = false;
        matchButton.setToggleState (false, dontSendNotification);
        status = std::abs (err) < 0.3f ? "Matched: " + String (integrated, 1) + " LUFS (drive " + String (current, 1) + " dB)"
                                       : "Stopped near target (" + String (integrated, 1) + " LUFS). Limiter is at its limit?";
        return;
    }
    // A limiter gives back less than dB-for-dB once it is working hard, so overshoot the correction a little when raising.
    const float step = err > 0.0f ? err * (1.0f + 0.15f * (float) matchIteration) : err;
    const float next = jlimit (range.start, range.end, current + step);
    proc.undoManager.beginNewTransaction();
    drive->beginChangeGesture();
    drive->setValueNotifyingHost (range.convertTo0to1 (next));
    drive->endChangeGesture();
    ++matchIteration;
    proc.requestLoudnessReset();
    matchListenStart = Time::getMillisecondCounterHiRes();
    status = "Drive " + String (current, 1) + " -> " + String (next, 1) + " dB, re-measuring...";
    if (next == range.end && err > 0.0f) { matching = false; matchButton.setToggleState (false, dontSendNotification); status = "Drive at maximum (24 dB)"; }
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
    d.makeup = m.autoMakeup.load();
    d.match = m.matchGain.load();
    d.m = m.momentary.load(); d.s = m.shortTerm.load(); d.i = m.integrated.load();
    d.corr = m.correlation.load();
    if (matching) stepAutoMatch();
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
    const int w4 = row.getWidth() / 4;
    readout (row.removeFromLeft (w4), "MOMENTARY", fmtLufs (d.m), colours::text);
    readout (row.removeFromLeft (w4), "SHORT", fmtLufs (d.s), colours::text);
    const bool near = d.i > -69.0f && std::abs (d.i - goal) < 1.0f;
    readout (row.removeFromLeft (w4), "INTEGRATED", fmtLufs (d.i), near ? colours::accent : colours::accent2);
    readout (row, "TRUE PEAK", d.tp < -100.0f ? String ("--") : String (d.tp, 1),
             d.tp > proc.state.getRawParameterValue ("limCeiling")->load() + 0.05f ? colours::warn : colours::text);

    area.removeFromTop (100); // target knob and buttons (positioned in resized)
    g.setColour (colours::dim);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (status, area.removeFromTop (30), Justification::centredLeft, 2);

    auto bar = [&] (const String& name, float value, float minV, float maxV, bool fromRight, Colour c)
    {
        auto b = area.removeFromTop (22);
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText (name, b.removeFromLeft (70), Justification::centredLeft);
        auto txt = b.removeFromRight (50);
        auto track = b.reduced (0, 6).toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (track, 3.0f);
        const float frac = jlimit (0.0f, 1.0f, (value - minV) / (maxV - minV));
        auto fill = fromRight ? track.withLeft (track.getRight() - track.getWidth() * frac) : track.withWidth (track.getWidth() * frac);
        g.setColour (c);
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (colours::text);
        g.drawText (fromRight ? (value < 0.05f ? String ("0.0") : String (-value, 1)) : (value < -100.0f ? String ("-inf") : String (value, 1)),
                    txt, Justification::centredRight);
    };
    bar ("Input", d.in, -48.0f, 0.0f, false, colours::accent.withAlpha (0.8f));
    bar ("Output", d.out, -48.0f, 0.0f, false, colours::accent);
    area.removeFromTop (8);
    g.setColour (colours::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText ("GAIN REDUCTION", area.removeFromTop (20), Justification::centredLeft);
    bar ("Glue", d.comp, 0.0f, 12.0f, true, colours::accent2);
    bar ("Limiter", d.lim, 0.0f, 12.0f, true, colours::warn);
    area.removeFromTop (6);

    auto line = [&] (const String& name, const String& value)
    {
        auto b = area.removeFromTop (20);
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText (name, b, Justification::centredLeft);
        g.setColour (colours::text);
        g.drawText (value, b, Justification::centredRight);
    };
    line ("Glue auto makeup", d.makeup > 0.05f ? "+" + String (d.makeup, 1) + " dB" : String ("off / 0.0 dB"));
    line ("Bypass level match", (d.match >= 0 ? "+" : "") + String (d.match, 1) + " dB");
    area.removeFromTop (6);

    // Correlation (+1 mono, 0 wide, below 0 out of phase)
    {
        auto b = area.removeFromTop (22);
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText ("Correlation", b.removeFromLeft (70), Justification::centredLeft);
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
      stereo (p.state, "Stereo", "stereoOn", 2),
      sat (p.state, "Saturate", "satOn", 2),
      mb (p.state, "Multiband", "mbOn", 7),
      dyn (p.state, "Dynamic EQ", "dynOn", 5),
      glue (p.state, "Glue Comp", "compOn", 5),
      lim (p.state, "Limiter", "limOn", 4),
      analyzer (p),
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
    eq.addChoice ("eqMode", "Mode");
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
    mb.addChoice ("mbMode", "Mode");
    auto& m = p.meters;
    mb.setHeaderComponent (std::make_unique<mbgui::GrStrip> (std::vector<mbgui::GrStrip::Item> {
                               { "Lo", [&m] { return m.bandGr[0].load(); } },
                               { "Mid", [&m] { return m.bandGr[1].load(); } },
                               { "Hi", [&m] { return m.bandGr[2].load(); } } }),
                           330);

    dyn.addKnob ("dyn0Hz", "1 Freq");
    dyn.addKnob ("dyn0Q", "1 Q");
    dyn.addKnob ("dyn0Thresh", "1 Thr");
    dyn.addKnob ("dyn0Range", "1 Range");
    dyn.addKnob ("dynAttack", "Attack");
    dyn.addKnob ("dyn1Hz", "2 Freq");
    dyn.addKnob ("dyn1Q", "2 Q");
    dyn.addKnob ("dyn1Thresh", "2 Thr");
    dyn.addKnob ("dyn1Range", "2 Range");
    dyn.addKnob ("dynRelease", "Release");
    dyn.setHeaderComponent (std::make_unique<mbgui::GrStrip> (std::vector<mbgui::GrStrip::Item> {
                                { "1", [&m] { return m.dynGr[0].load(); } },
                                { "2", [&m] { return m.dynGr[1].load(); } } }),
                            190);

    glue.addKnob ("compThresh", "Threshold");
    glue.addKnob ("compRatio", "Ratio");
    glue.addKnob ("compAttack", "Attack");
    glue.addKnob ("compRelease", "Release");
    glue.addKnob ("compKnee", "Knee");
    glue.addKnob ("compScHpf", "SC HPF");
    glue.addKnob ("compMakeup", "Makeup");
    glue.addKnob ("compMix", "Mix");
    glue.addToggle ("compAutoRel", "Auto Rel");
    glue.addToggle ("compAutoMakeup", "Auto Gain");
    glue.setHeaderComponent (std::make_unique<mbgui::GrStrip> (std::vector<mbgui::GrStrip::Item> {
                                 { "GR", [&m] { return m.compGr.load(); } } }),
                             120);

    lim.addKnob ("limGain", "Drive");
    lim.addKnob ("limCeiling", "Ceiling");
    lim.addKnob ("limRelease", "Release");
    lim.addChoice ("lookahead", "Lookahead");
    lim.addToggle ("autoRelease", "Auto Rel");
    lim.addToggle ("truePeak", "True Peak");
    lim.addChoice ("dither", "Dither");
    lim.setHeaderComponent (std::make_unique<mbgui::GrStrip> (std::vector<mbgui::GrStrip::Item> {
                                { "GR", [&m] { return m.limGr.load(); } } }),
                            120);

    // Header bar
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setTooltip ("Factory and user presets. Starting points, not finished masters.");
    presetBox.onChange = [this] { presetChosen(); };
    saveButton.setTooltip ("Save the current settings as a user preset.");
    saveButton.onClick = [this] { savePreset(); };
    undoButton.setTooltip ("Undo the last change.");
    redoButton.setTooltip ("Redo.");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    const char* names[] { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = snapButtons[(size_t) i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (false);
        b.setTooltip ("Snapshot " + String (names[i]) + ": stores the current settings in the active slot and recalls this one. "
                      "An empty slot starts as a copy of the current settings.");
        b.onClick = [this, i] { proc.selectSnapshot (i); };
        content.addAndMakeVisible (b);
    }
    bypassButton.setTooltip ("Hear the unprocessed input at the same loudness as the processed output (level-matched bypass). "
                             "Judges the processing, not the volume change.");
    deltaButton.setTooltip ("Hear only what the processing adds or removes (processed minus level-matched input).");
    bypassButton.onClick = [this] { setCompare (proc.compare.load() == MasterBlasterProcessor::compareBypass ? 0 : MasterBlasterProcessor::compareBypass); };
    deltaButton.onClick = [this] { setCompare (proc.compare.load() == MasterBlasterProcessor::compareDelta ? 0 : MasterBlasterProcessor::compareDelta); };
    for (auto* c : std::initializer_list<Component*> { &presetBox, &saveButton, &undoButton, &redoButton, &bypassButton, &deltaButton })
        content.addAndMakeVisible (c);

    for (auto* c : std::initializer_list<Component*> { &eq, &stereo, &sat, &mb, &dyn, &glue, &lim, &analyzer, &meters })
        content.addAndMakeVisible (c);
    addAndMakeVisible (content);
    setLookAndFeel (&lnf); // after the children exist, so they all pick it up
    addMouseListener (this, true); // every click starts a new undo step

    // Rows: [EQ | Limiter] [Multiband | Glue] [Dynamic EQ | Stereo | Saturate] [Analyzer], meters on the right.
    leftW = jmax (eq.preferredWidth() + gap + lim.preferredWidth(), mb.preferredWidth() + gap + glue.preferredWidth());
    rowH = std::max ({ eq.preferredHeight(), lim.preferredHeight(), mb.preferredHeight(), glue.preferredHeight(), dyn.preferredHeight() });
    baseW = gap + leftW + gap + metersW + gap;
    baseH = headerH + 3 * (rowH + gap) + analyzerH + gap;

    refreshPresetMenu();
    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / baseH);
    setSize ((int) (baseW * 0.85), (int) (baseH * 0.85));
    startTimerHz (10);
}

MasterBlasterEditor::~MasterBlasterEditor()
{
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void MasterBlasterEditor::mouseDown (const MouseEvent& e)
{
    if (e.originalComponent != &undoButton && e.originalComponent != &redoButton)
        proc.undoManager.beginNewTransaction();
}

void MasterBlasterEditor::timerCallback()
{
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    for (int i = 0; i < 4; ++i)
    {
        auto& b = snapButtons[(size_t) i];
        const bool active = proc.getActiveSnapshot() == i;
        b.setToggleState (active, dontSendNotification);
        b.setAlpha (active || proc.snapshotUsed (i) ? 1.0f : 0.55f);
    }
    bypassButton.setToggleState (proc.compare.load() == MasterBlasterProcessor::compareBypass, dontSendNotification);
    deltaButton.setToggleState (proc.compare.load() == MasterBlasterProcessor::compareDelta, dontSendNotification);
    if (presetBox.getText() != proc.currentPresetName && ! presetBox.isPopupActive())
        presetBox.setText (proc.currentPresetName, dontSendNotification);
}

void MasterBlasterEditor::setCompare (int mode) { proc.compare.store (mode); timerCallback(); }

void MasterBlasterEditor::refreshPresetMenu()
{
    presetBox.clear (dontSendNotification);
    const auto& factory = mbp::factoryPresets();
    presetBox.addSectionHeading ("Factory");
    for (size_t i = 0; i < factory.size(); ++i) presetBox.addItem (factory[i].name, (int) i + 1);
    userPresetFiles = proc.getUserPresets();
    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), 1000 + i);
    }
    presetBox.setText (proc.currentPresetName, dontSendNotification);
}

void MasterBlasterEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;
    if (id >= 1000) proc.loadUserPreset (userPresetFiles[id - 1000]);
    else proc.setCurrentProgram (id - 1);
    presetBox.setText (proc.currentPresetName, dontSendNotification);
}

void MasterBlasterEditor::savePreset()
{
    saveDialog = std::make_unique<AlertWindow> ("Save preset", "Name for this preset:", MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", proc.currentPresetName);
    saveDialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    saveDialog->enterModalState (true, ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (proc.saveUserPreset (name)) refreshPresetMenu();
        }
        saveDialog.reset();
    }), false);
}

void MasterBlasterEditor::paint (Graphics& g)
{
    g.fillAll (mbgui::colours::bg);
    const float scale = (float) getWidth() / (float) baseW;
    g.addTransform (AffineTransform::scale (scale));
    g.setColour (mbgui::colours::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("MASTER BLASTER", 12, 0, 220, headerH, Justification::centredLeft);
}

void MasterBlasterEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (AffineTransform::scale (scale));

    // Header: presets | snapshots | undo/redo | compare
    {
        auto h = Rectangle<int> (230, 0, gap + leftW - 230, headerH).withSizeKeepingCentre (gap + leftW - 230, 28);
        presetBox.setBounds (h.removeFromLeft (230));
        h.removeFromLeft (6);
        saveButton.setBounds (h.removeFromLeft (56));
        h.removeFromLeft (24);
        for (auto& b : snapButtons) { b.setBounds (h.removeFromLeft (30)); h.removeFromLeft (3); }
        h.removeFromLeft (21);
        undoButton.setBounds (h.removeFromLeft (52));
        h.removeFromLeft (4);
        redoButton.setBounds (h.removeFromLeft (52));
    }
    {
        // Compare buttons sit above the meter column
        auto c = Rectangle<int> (gap + leftW + gap, 0, metersW, headerH).withSizeKeepingCentre (metersW, 28);
        bypassButton.setBounds (c.removeFromLeft (c.getWidth() * 2 / 3 - 3));
        c.removeFromLeft (6);
        deltaButton.setBounds (c);
    }

    auto placeRow = [&] (std::initializer_list<mbgui::Panel*> panels, int y)
    {
        int x = gap;
        const int fixed = [&] { int w = 0; for (auto* p : panels) w += p->preferredWidth(); return w; }();
        const int extra = leftW - fixed - gap * ((int) panels.size() - 1);
        const int last = (int) panels.size() - 1;
        int i = 0;
        for (auto* p : panels)
        {
            // Spare width goes to the last panel(s); here it is shared between all but the first.
            int w = p->preferredWidth();
            if (i > 0) w += extra / last + (i == last ? extra % jmax (1, last) : 0);
            p->setBounds (x, y, w, rowH);
            x += w + gap;
            ++i;
        }
    };
    int y = headerH;
    placeRow ({ &eq, &lim }, y);            y += rowH + gap;
    placeRow ({ &mb, &glue }, y);           y += rowH + gap;
    placeRow ({ &dyn, &stereo, &sat }, y);  y += rowH + gap;
    analyzer.setBounds (gap, y, leftW, analyzerH);
    meters.setBounds (gap + leftW + gap, headerH, metersW, baseH - headerH - gap);
}
