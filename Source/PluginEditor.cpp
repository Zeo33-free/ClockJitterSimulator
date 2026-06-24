#include "PluginEditor.h"

// ==============================================================================
// Custom LookAndFeel
// ==============================================================================
JitterLookAndFeel::JitterLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (0xff00d4ff));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2a4a));
    setColour (juce::Slider::thumbColourId,               juce::Colour (0xff00d4ff));
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xffe0e0e0));
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (0x00000000));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (0x00000000));
}

juce::Slider::SliderLayout JitterLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    // Let the rotary fill the entire slider area — no text box
    auto layout = LookAndFeel_V4::getSliderLayout (slider);
    layout.sliderBounds = slider.getLocalBounds();
    layout.textBoxBounds = juce::Rectangle<int>();
    return layout;
}

juce::Label* JitterLookAndFeel::createSliderTextBox (juce::Slider&)
{
    return nullptr; // We display value elsewhere
}

void JitterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                          int w, int h, float sliderPos,
                                          float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    juce::ignoreUnused (slider);
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (w),
                                                 static_cast<float> (h));
    const float radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);

    // Jitter intensity drives visual thickness
    const float intensity = std::sqrt (sliderPos);  // smooth ramp

    // Arc radius — scaled to leave room for glow stroke beyond the bounds
    const float arcR = radius * 0.88f;

    // Track (outer ring) — fades from dark to medium as jitter increases
    {
        juce::Path track;
        track.addCentredArc (centreX, centreY, arcR, arcR,
                             0.0f, startAngle, endAngle, true);
        juce::Colour trackCol = juce::Colour (0xff2a2a4a).interpolatedWith (
                                juce::Colour (0xff3a3a60), intensity);
        g.setColour (trackCol);
        g.strokePath (track, juce::PathStrokeType (radius * 0.09f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Filled arc — thickness and glow both scale with jitter
    if (sliderPos > 0.001f)
    {
        juce::Path filled;
        filled.addCentredArc (centreX, centreY, arcR, arcR,
                              0.0f, startAngle, toAngle, true);

        // Glow layer — grows from subtle to bloom (max ~0.15×radius)
        juce::Path glowPath = filled;
        juce::Colour glowCol = juce::Colour (0x2200d4ff).interpolatedWith (
                               juce::Colour (0xaa00d4ff), intensity);
        g.setColour (glowCol);
        g.strokePath (glowPath, juce::PathStrokeType (
                         radius * (0.05f + intensity * 0.10f),
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Main arc — color shifts from cyan to cyan-magenta, thickness max ~0.10×radius
        juce::Colour arcStart = juce::Colour (0xff00a0c0).interpolatedWith (
                                juce::Colour (0xff00ffff), intensity);
        juce::Colour arcEnd   = juce::Colour (0xff0040a0).interpolatedWith (
                                juce::Colour (0xff0080ff), intensity);
        juce::ColourGradient grad (arcStart, centreX, centreY,
                                   arcEnd, centreX + radius, centreY, true);
        g.setGradientFill (grad);
        g.strokePath (filled, juce::PathStrokeType (
                         radius * (0.04f + intensity * 0.06f),
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer (thin line with glow) — glow grows with jitter
    juce::Path pointer;
    const float ptrLen = radius * 0.65f;
    const float ptrCos = std::cos (toAngle);
    const float ptrSin = std::sin (toAngle);
    pointer.addLineSegment (
        juce::Line<float> (
            centreX - ptrSin * ptrLen * 0.2f,
            centreY + ptrCos * ptrLen * 0.2f,
            centreX + ptrSin * ptrLen,
            centreY - ptrCos * ptrLen),
        0.0f);

    // Pointer glow (max ~0.10×radius)
    g.setColour (juce::Colour (0x4400d4ff).interpolatedWith (
                 juce::Colour (0xbb00ffff), intensity));
    g.strokePath (pointer, juce::PathStrokeType (
                     radius * (0.03f + intensity * 0.07f),
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Pointer core
    g.setColour (juce::Colour (0xffd0e8ff).interpolatedWith (
                 juce::Colour (0xffffffff), intensity));
    g.strokePath (pointer, juce::PathStrokeType (
                     radius * (0.028f + intensity * 0.02f),
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Center dot — brightens with jitter
    g.setColour (juce::Colour (0xff00d4ff).interpolatedWith (
                 juce::Colour (0xff40ffff), intensity));
    g.fillEllipse (centreX - radius * 0.12f, centreY - radius * 0.12f,
                   radius * 0.24f, radius * 0.24f);
}

// ==============================================================================
// Jitter Display 鈥?animated wavering line showing jitter activity
// ==============================================================================
JitterDisplay::JitterDisplay (JitterClockSimAudioProcessor& proc)
    : processor (proc)
{
    history.fill (0.0f);
}

void JitterDisplay::timerCallback()
{
    // Push new jitter value
    float val = processor.getCurrentJitterDisplay().load (std::memory_order_relaxed);
    history[size_t (writeIndex)] = val;
    writeIndex = (writeIndex + 1) % kHistorySize;
    repaint();
}

void JitterDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float midY = h * 0.5f;

    // Jitter intensity for color modulation
    auto* param = processor.apvts.getRawParameterValue ("jitter");
    float jitterNorm = param != nullptr ? param->load() / 10.0f : 0.0f;

    // Background — warms up with jitter
    g.setColour (juce::Colour (0xff0d0d1a).interpolatedWith (
                 juce::Colour (0xff14142c), jitterNorm));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colour (0xff1a1a2e).interpolatedWith (
                 juce::Colour (0xff2a2a50), jitterNorm));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    // Grid lines
    g.setColour (juce::Colour (0x22ffffff).interpolatedWith (
                 juce::Colour (0x44ffffff), jitterNorm));
    for (int i = 1; i < 4; ++i)
    {
        float gy = h * i / 4.0f;
        g.drawLine (0.0f, gy, w, gy, 0.5f);
    }

    // Center line
    g.setColour (juce::Colour (0x44ffffff).interpolatedWith (
                 juce::Colour (0x88ffffff), jitterNorm));
    g.drawLine (0.0f, midY, w, midY, 1.0f);

    // Draw jitter history
    if (w < 2.0f) return;

    juce::Path wavePath;
    bool first = true;
    float dx = w / static_cast<float> (kHistorySize - 1);

    for (int i = 0; i < kHistorySize; ++i)
    {
        // Read backward from most recent: i=0 → newest, i=kHistorySize-1 → oldest
        int idx = (writeIndex - 1 - i + kHistorySize) % kHistorySize;
        float val = history[size_t (idx)];
        // Map: val is in samples [-jitterAmt, +jitterAmt], we clip display to 卤5 samples
        float y = midY - (val / 5.0f) * midY * 0.85f;
        float x = static_cast<float> (i) * dx;

        if (first)
            wavePath.startNewSubPath (x, y);
        else
            wavePath.lineTo (x, y);
        first = false;
    }

    // Glow layer
    g.setColour (juce::Colour (0x4400d4ff).interpolatedWith (
                 juce::Colour (0xaa00ffff), jitterNorm));
    g.strokePath (wavePath, juce::PathStrokeType (3.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Core line
    g.setColour (juce::Colour (0xff00d4ff).interpolatedWith (
                 juce::Colour (0xff40ffff), jitterNorm));
    g.strokePath (wavePath, juce::PathStrokeType (1.2f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Labels
    g.setFont (juce::FontOptions ("Consolas", 10.0f, juce::Font::plain));
    g.setColour (juce::Colour (0xff505070).interpolatedWith (
                 juce::Colour (0xff8080c0), jitterNorm));
    g.drawText ("+5",  juce::Rectangle<float> (2.0f, 1.0f, 30.0f, 14.0f),
                juce::Justification::topLeft);
    g.drawText ("0",   juce::Rectangle<float> (2.0f, midY - 7.0f, 30.0f, 14.0f),
                juce::Justification::centredLeft);
    g.drawText ("-5",  juce::Rectangle<float> (2.0f, h - 16.0f, 30.0f, 14.0f),
                juce::Justification::bottomLeft);
    g.drawText ("JITTER (samples)", juce::Rectangle<float> (w - 80.0f, 1.0f, 78.0f, 14.0f),
                juce::Justification::topRight);
}

void JitterDisplay::resized()
{
}

void JitterDisplay::start()
{
    startTimerHz (30);
}

// ==============================================================================
// Main Editor
// ==============================================================================
JitterClockSimAudioProcessorEditor::JitterClockSimAudioProcessorEditor (
    JitterClockSimAudioProcessor& p)
    : AudioProcessorEditor (p)
    , audioProc (p)
    , jitterViz (p)
{
    // Set resizeable
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (kDefaultWidth, kDefaultHeight);

    // Apply custom LookAndFeel
    setLookAndFeel (&lookAndFeel);

    // --- Title label ---
    titleLabel.setText ("CLOCK JITTER", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Segoe UI", 26.0f, juce::Font::bold)
                            );
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0e0e0));
    addAndMakeVisible (titleLabel);

    // --- Jitter knob ---
    jitterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    jitterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    jitterSlider.setRange (0.0, 10.0, 0.001);
    jitterSlider.setDoubleClickReturnValue (true, 0.0);
    jitterSlider.setLookAndFeel (&lookAndFeel);
    jitterSlider.setColour (juce::Slider::backgroundColourId,
                            juce::Colour (0x00000000));
    addAndMakeVisible (jitterSlider);

    sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProc.apvts, "jitter", jitterSlider);

    // --- Value label (numeric readout) ---
    valueLabel.setText ("0.000", juce::dontSendNotification);
    valueLabel.setFont (juce::FontOptions ("Consolas", 32.0f, juce::Font::bold));
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00d4ff));
    addAndMakeVisible (valueLabel);

    // --- Unit label ---
    unitLabel.setText ("samples", juce::dontSendNotification);
    unitLabel.setFont (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain)
                           );
    unitLabel.setJustificationType (juce::Justification::centred);
    unitLabel.setColour (juce::Label::textColourId, juce::Colour (0xff505070));
    addAndMakeVisible (unitLabel);

    // --- Jitter visualization ---
    addAndMakeVisible (jitterViz);
    jitterViz.start();  // start after visible

    // --- OS selector ---
    osCombo.addItemList ({ "50x", "100x", "250x", "1000x" }, 1);
    osCombo.setSelectedId (1);  // default: 50x
    osCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a1a2e));
    osCombo.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe0e0e0));
    osCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2a4a));
    osCombo.setColour (juce::ComboBox::focusedOutlineColourId, juce::Colour (0xff00d4ff));
    osCombo.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff00d4ff));
    osCombo.setColour (juce::ComboBox::buttonColourId, juce::Colour (0x00000000));
    osCombo.setLookAndFeel (&lookAndFeel);
    osCombo.onChange = [this]
    {
        int id = osCombo.getSelectedId();
        juce::String precision;
        switch (id)
        {
            case 1: precision = "±0.02 samples";  break;
            case 2: precision = "±0.01 samples";  break;
            case 3: precision = "±0.004 samples"; break;
            case 4: precision = "±0.001 samples"; break;
        }
        precisionLabel.setText (precision, juce::dontSendNotification);
    };
    addAndMakeVisible (osCombo);

    // --- Precision label ---
    precisionLabel.setFont (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain));
    precisionLabel.setJustificationType (juce::Justification::centredRight);
    precisionLabel.setColour (juce::Label::textColourId, juce::Colour (0xff707090));
    addAndMakeVisible (precisionLabel);
    osCombo.onChange();  // trigger initial precision text

    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProc.apvts, "osrate", osCombo);

    // --- Subtitle label ---
    subtitleLabel.setText ("SIMULATOR", juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain)
                               );
    subtitleLabel.setJustificationType (juce::Justification::centred);
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff707090));
    addAndMakeVisible (subtitleLabel);

    // Timer to update value display
    startTimerHz (15);
}

JitterClockSimAudioProcessorEditor::~JitterClockSimAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

// ==============================================================================
void JitterClockSimAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());
    const float t = currentJitterNorm;

    // Background — navy shifts towards deep blue as jitter rises
    juce::Colour bgTop = bgDark.interpolatedWith (juce::Colour (0xff141430), t);
    juce::Colour bgBot = bgMid.interpolatedWith (juce::Colour (0xff242448), t);
    juce::ColourGradient bgGrad (bgTop, 0.0f, 0.0f, bgBot, 0.0f, h, false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    // Top accent glow — intensifies with jitter
    juce::ColourGradient topGlow (
        juce::Colour (0x2200d4ff).interpolatedWith (juce::Colour (0x8800d4ff), t),
        0.0f, 0.0f,
        juce::Colour (0x00000000), 0.0f, h * 0.4f, false);
    g.setGradientFill (topGlow);
    g.fillRect (0.0f, 0.0f, w, h * 0.4f);

    // Subtle border
    g.setColour (juce::Colour (0xff18182e).interpolatedWith (
                 juce::Colour (0xff28285a), t));
    g.drawRect (0.0f, 0.0f, w, h, 1.0f);

    // Inner accent line at top
    g.setColour (juce::Colour (0x3300d4ff).interpolatedWith (
                 juce::Colour (0xbb00d4ff), t));
    g.drawLine (0.0f, 0.0f, w, 0.0f, 2.0f);
}

// ==============================================================================
void JitterClockSimAudioProcessorEditor::resized()
{
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());
    const float scale = juce::jmin (w / static_cast<float> (kDefaultWidth),
                                    h / static_cast<float> (kDefaultHeight));

    // Title row geometry — text center = h*0.095f
    const float titleFontSize = juce::jlimit (18.0f, 38.0f, 26.0f * scale);
    titleLabel.setFont (juce::FontOptions ("Segoe UI", titleFontSize, juce::Font::bold));
    const float titleTop  = h * 0.045f;
    const float titleH    = h * 0.10f;
    const float titleMid  = titleTop + titleH * 0.5f;   // text vertical center
    titleLabel.setBounds (0, static_cast<int> (titleTop),
                          getWidth(), static_cast<int> (titleH));

    // OS combo — right side, vertically centered with title text
    const float osComboW = juce::jlimit (55.0f, 100.0f, 80.0f * scale);
    const float osComboH = juce::jlimit (17.0f, 26.0f, 22.0f * scale);
    const float osRight = w - w * 0.05f;
    osCombo.setBounds (static_cast<int> (osRight - osComboW),
                       static_cast<int> (titleMid - osComboH * 0.5f),
                       static_cast<int> (osComboW),
                       static_cast<int> (osComboH));

    // Subtitle row geometry — text center = h*0.14f
    const float subFontSize = juce::jlimit (10.0f, 16.0f, 13.0f * scale);
    subtitleLabel.setFont (juce::FontOptions ("Segoe UI", subFontSize, juce::Font::plain));
    const float subTop = h * 0.12f;
    const float subH   = h * 0.04f;
    const float subMid = subTop + subH * 0.5f;
    subtitleLabel.setBounds (0, static_cast<int> (subTop),
                             getWidth(), static_cast<int> (subH));

    // Precision label — right side, vertically centered with subtitle
    const float precFontSize = juce::jlimit (10.0f, 16.0f, 13.0f * scale);
    const float precH = h * 0.035f;
    precisionLabel.setFont (juce::FontOptions ("Segoe UI", precFontSize, juce::Font::plain));
    precisionLabel.setBounds (static_cast<int> (osRight - osComboW),
                              static_cast<int> (subMid - precH * 0.5f),
                              static_cast<int> (osComboW),
                              static_cast<int> (precH));

    // Knob in the upper-mid area
    const float knobSize = juce::jlimit (120.0f, 260.0f, 200.0f * scale);
    const float knobX = (w - knobSize) * 0.5f;
    const float knobY = h * 0.22f;
    jitterSlider.setBounds (static_cast<int> (knobX), static_cast<int> (knobY),
                            static_cast<int> (knobSize), static_cast<int> (knobSize));

    // Value label below knob
    const float valFontSize = juce::jlimit (22.0f, 44.0f, 32.0f * scale);
    valueLabel.setFont (juce::FontOptions ("Consolas", valFontSize, juce::Font::bold));
    valueLabel.setBounds (0, static_cast<int> (knobY + knobSize + h * 0.015f),
                          getWidth(), static_cast<int> (h * 0.08f));

    // Unit label
    const float unitFontSize = juce::jlimit (10.0f, 16.0f, 13.0f * scale);
    unitLabel.setFont (juce::FontOptions ("Segoe UI", unitFontSize, juce::Font::plain));
    unitLabel.setBounds (0, static_cast<int> (knobY + knobSize + h * 0.09f),
                         getWidth(), static_cast<int> (h * 0.04f));

    // Jitter visualization at the bottom
    const float vizMargin = w * 0.06f;
    const float vizY = unitLabel.getBottom() + h * 0.02f;
    const float vizH = juce::jlimit (60.0f, 140.0f, h * 0.18f);
    jitterViz.setBounds (static_cast<int> (vizMargin), static_cast<int> (vizY),
                         static_cast<int> (w - 2.0f * vizMargin), static_cast<int> (vizH));
}

// ==============================================================================
// Timer callback 鈥?poll parameter value for the readout (message thread safe)
// ==============================================================================
void JitterClockSimAudioProcessorEditor::timerCallback()
{
    auto* param = audioProc.apvts.getRawParameterValue ("jitter");
    if (param != nullptr)
    {
        float v = param->load();
        valueLabel.setText (juce::String (v, 3), juce::dontSendNotification);

        float t = v / 10.0f;  // normalize 0–1
        if (std::abs (t - currentJitterNorm) > 0.002f)
        {
            currentJitterNorm = t;

            // Title: white → bright cyan
            titleLabel.setColour (juce::Label::textColourId,
                juce::Colour (0xffe0e0e0).interpolatedWith (juce::Colour (0xff00ffff), t));

            // Value readout: cyan → hot pink
            valueLabel.setColour (juce::Label::textColourId,
                juce::Colour (0xff00d4ff).interpolatedWith (juce::Colour (0xffff40ff), t));

            // Subtitle + precision: dim grey → cyan-tinged
            juce::Colour subCol = juce::Colour (0xff707090).interpolatedWith (
                                  juce::Colour (0xff40c0e0), t);
            subtitleLabel.setColour (juce::Label::textColourId, subCol);
            precisionLabel.setColour (juce::Label::textColourId, subCol);

            // Unit label: dim grey → slightly brighter
            unitLabel.setColour (juce::Label::textColourId,
                juce::Colour (0xff505070).interpolatedWith (juce::Colour (0xff8080b0), t));

            // OS combo — background/text/outline/arrow warm up with jitter
            osCombo.setColour (juce::ComboBox::backgroundColourId,
                juce::Colour (0xff1a1a2e).interpolatedWith (juce::Colour (0xff242450), t));
            osCombo.setColour (juce::ComboBox::textColourId,
                juce::Colour (0xffe0e0e0).interpolatedWith (juce::Colour (0xff40ffff), t));
            osCombo.setColour (juce::ComboBox::outlineColourId,
                juce::Colour (0xff2a2a4a).interpolatedWith (juce::Colour (0xff0040ff), t));
            osCombo.setColour (juce::ComboBox::arrowColourId,
                juce::Colour (0xff00d4ff).interpolatedWith (juce::Colour (0xff80ffff), t));

            repaint();
        }
    }
}
