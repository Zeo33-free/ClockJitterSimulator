#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// ==============================================================================
// Custom LookAndFeel — modern dark theme with cyan glow
// ==============================================================================
class JitterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JitterLookAndFeel();
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override;
    juce::Label* createSliderTextBox (juce::Slider& slider) override;
};

// ==============================================================================
// Jitter visualization component — shows real-time jitter as a wavering line
// ==============================================================================
class JitterDisplay : public juce::Component, private juce::Timer
{
public:
    JitterDisplay (JitterClockSimAudioProcessor& proc);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void start();

private:
    void timerCallback() override;

    JitterClockSimAudioProcessor& processor;
    static constexpr int kHistorySize = 256;
    std::array<float, kHistorySize> history {};
    int writeIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JitterDisplay)
};

// ==============================================================================
// Main editor
// ==============================================================================
class JitterClockSimAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit JitterClockSimAudioProcessorEditor (JitterClockSimAudioProcessor& p);
    ~JitterClockSimAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    JitterClockSimAudioProcessor& audioProc;

    JitterLookAndFeel lookAndFeel;
    juce::Slider jitterSlider;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label valueLabel;
    juce::Label unitLabel;
    juce::Label precisionLabel;
    JitterDisplay jitterViz;

    juce::ComboBox osCombo;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;

    // Jitter-driven UI intensity (0.0–1.0)
    float currentJitterNorm = 0.0f;

    // Resize limits
    static constexpr int kMinWidth  = 320;
    static constexpr int kMinHeight = 420;
    static constexpr int kMaxWidth  = 800;
    static constexpr int kMaxHeight = 1000;
    static constexpr int kDefaultWidth  = 400;
    static constexpr int kDefaultHeight = 520;

    // Colors
    juce::Colour bgDark      { 0xff0d0d1a };
    juce::Colour bgMid       { 0xff1a1a2e };
    juce::Colour accentCyan  { 0xff00d4ff };
    juce::Colour accentGlow  { 0x4400d4ff };
    juce::Colour textBright  { 0xffe0e0e0 };
    juce::Colour textDim     { 0xff707090 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JitterClockSimAudioProcessorEditor)
};
