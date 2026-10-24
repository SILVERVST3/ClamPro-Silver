#pragma once
#include "PluginProcessor.h"

class CalmaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CalmaEditor (CalmaProcessor&);
    ~CalmaEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (curveArea); }

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    CalmaProcessor& proc;
    std::array<Knob, 8> knobs;
    juce::ToggleButton deltaButton { "Delta" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttach;
    juce::Rectangle<int> curveArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalmaEditor)
};
