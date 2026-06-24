#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// ==============================================================================
// Clock Jitter Simulator — selectable oversampling + random jitter + cubic interpolation
//
// Pipeline per channel:
//   input → N× polyphase FIR ↑ → OS ring buffer
//         → jittered cubic read → output IIR lowpass → output
//   N ∈ {50, 100, 250, 1000}
// ==============================================================================

class JitterClockSimAudioProcessor : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    // ---- constants (sized for maximum oversampling) ----
    static constexpr int kMaxOversample = 1000;
    static constexpr int kTapsPerPhase   = 16;
    static constexpr int kMaxFIRLength   = kMaxOversample * kTapsPerPhase;   // 16000
    static constexpr int kInputBufSize   = 64;   // power of 2, >= kTapsPerPhase
    static constexpr int kInputMask      = kInputBufSize - 1;
    static constexpr int kOSBufSize      = 131072; // power of 2, >= kMaxFIRLength × 2
    static constexpr int kOSMask         = kOSBufSize - 1;

    // ==========================================================================
    JitterClockSimAudioProcessor();
    ~JitterClockSimAudioProcessor() override;

    // ---- AudioProcessor overrides ----
    void prepareToPlay (double sampleRate, int maxSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override           { return "Clock Jitter Simulator"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override;
    void setStateInformation (const void* data, int size) override;

    // ---- parameter access ----
    juce::AudioProcessorValueTreeState apvts;

    // ---- accessible DSP state for editor visualization ----
    std::atomic<float>& getCurrentJitterDisplay() { return currentJitterDisplay; }

private:
    // ---- FIR coefficients (sized for max OS; only first firLength used) ----
    std::array<float, kMaxFIRLength> firCoeffs;

    // ---- runtime oversampling state ----
    int oversample = 50;  // default 50×
    int firLength   = oversample * kTapsPerPhase;

    // ---- input ring buffers (per channel) ----
    std::array<float, kInputBufSize> inBufL;
    std::array<float, kInputBufSize> inBufR;
    int inPos = 0;

    // ---- oversampled ring buffers (per channel) ----
    std::vector<float> osBufL;
    std::vector<float> osBufR;

    // ---- OS-rate cursors (double for sub-ULP precision at high OS) ----
    double wpos = 0.0;  // write position
    double rpos = 0.0;  // read  position

    // ---- sample rate ----
    double currentSampleRate = 44100.0;

    // ---- RNG ----
    uint32_t rng = 1234567;
    inline float randUni();

    // ---- IIR lowpass (16th-order = 8 biquad cascade) ----
    static constexpr int kIIROrder    = 16;
    static constexpr int kIIRSections = kIIROrder / 2;
    std::array<juce::dsp::IIR::Filter<float>, kIIRSections> iirL;
    std::array<juce::dsp::IIR::Filter<float>, kIIRSections> iirR;

    // ---- jitter display value ----
    std::atomic<float> currentJitterDisplay { 0.0f };

    // ---- helper methods ----
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void buildFIR (int os);
    void setupIIR (double sampleRate);
    void applyOversample (int os, double sampleRate);
    void upsampleChannel (const std::array<float, kInputBufSize>& inBuf,
                          std::vector<float>& osBuf,
                          int phaseStart);
    float readCubic (const std::vector<float>& buf, double pos) const;

    // ---- guard against parameterChanged before prepareToPlay ----
    std::atomic<bool> prepared { false };

    // ---- parameter layout ----
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JitterClockSimAudioProcessor)
};
