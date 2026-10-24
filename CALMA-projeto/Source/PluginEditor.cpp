#include "PluginEditor.h"

CalmaEditor::CalmaEditor (CalmaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    struct Def { const char* id; const char* name; };
    const Def defs[8] = { { "depth", "DEPTH" }, { "sens", "SENSITIVITY" }, { "width", "WIDTH" },
                          { "attack", "ATTACK" }, { "release", "RELEASE" },
                          { "low", "LOW FREQ" }, { "high", "HIGH FREQ" }, { "mix", "MIX" } };

    const auto silver = juce::Colour (0xffc9ced6);
    const auto dark   = juce::Colour (0xff2b2f36);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff8a93a3));
        k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff4a505a));
        k.slider.setColour (juce::Slider::thumbColourId, silver);
        k.slider.setColour (juce::Slider::textBoxTextColourId, silver);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, dark);
        k.label.setText (defs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, juce::Colour (0xffe6e9ee));
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, defs[i].id, k.slider);
    }

    deltaButton.setColour (juce::ToggleButton::textColourId, silver);
    deltaButton.setColour (juce::ToggleButton::tickColourId, silver);
    addAndMakeVisible (deltaButton);
    deltaAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "delta", deltaButton);

    setSize (760, 460);
    startTimerHz (30);
}

void CalmaEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f48), 0.0f, 0.0f,
                                             juce::Colour (0xff1f2227), 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colour (0xffe6e9ee));
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("CALMA", 16, 8, 200, 28, juce::Justification::left);

    auto area = curveArea.toFloat();
    g.setColour (juce::Colour (0xff15171b));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colour (0xff4a505a));
    g.drawRoundedRectangle (area, 6.0f, 1.0f);

    const double fs = proc.getSampleRate();
    if (fs <= 0.0) return;

    constexpr float maxDb = 18.0f;
    juce::Path path;
    path.startNewSubPath (area.getX(), area.getY());
    for (int x = 0; x <= (int) area.getWidth(); ++x)
    {
        const float f   = 20.0f * std::pow (1000.0f, (float) x / area.getWidth()); // 20 Hz - 20 kHz
        const int   bin = juce::jlimit (0, CalmaProcessor::numBins - 1,
                                        (int) (f * (float) CalmaProcessor::fftSize / (float) fs));
        const float red = juce::jmin (maxDb, proc.displayReduction[(size_t) bin].load (std::memory_order_relaxed));
        path.lineTo (area.getX() + (float) x, area.getY() + (red / maxDb) * area.getHeight());
    }
    path.lineTo (area.getRight(), area.getY());
    path.closeSubPath();

    g.setColour (juce::Colour (0x66c9ced6));
    g.fillPath (path);
    g.setColour (juce::Colour (0xffc9ced6));
    g.strokePath (path, juce::PathStrokeType (1.2f));

    g.setColour (juce::Colour (0xff8a93a3));
    g.setFont (11.0f);
    const float freqs[] = { 100.0f, 1000.0f, 10000.0f };
    const char* names[] = { "100", "1k", "10k" };
    for (int i = 0; i < 3; ++i)
    {
        const float x = area.getX() + area.getWidth() * std::log10 (freqs[i] / 20.0f) / 3.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.drawText (names[i], (int) x + 3, (int) area.getBottom() - 16, 40, 14, juce::Justification::left);
    }
}

void CalmaEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (32);
    curveArea = r.removeFromTop (200);
    r.removeFromTop (12);

    deltaButton.setBounds (getWidth() - 96, 12, 80, 24);

    const int w = r.getWidth() / 4;
    const int h = r.getHeight() / 2;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (int) (i % 4) * w, r.getY() + (int) (i / 4) * h, w, h);
        knobs[i].label.setBounds (cell.removeFromTop (16));
        knobs[i].slider.setBounds (cell);
    }
}
