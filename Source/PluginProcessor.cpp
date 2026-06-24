#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
JitterClockSimAudioProcessor::JitterClockSimAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    buildFIR (oversample);
    osBufL.resize (kOSBufSize, 0.0f);
    osBufR.resize (kOSBufSize, 0.0f);

    apvts.addParameterListener ("osrate", this);
}

JitterClockSimAudioProcessor::~JitterClockSimAudioProcessor()
{
    apvts.removeParameterListener ("osrate", this);
}

// ==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
JitterClockSimAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto attributes = juce::AudioParameterFloatAttributes()
        .withLabel ("samples")
        .withCategory (juce::AudioProcessorParameter::genericParameter)
        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 3) + " samples"; })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID ("jitter", 1),
        "Jitter",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.001f, 0.5f),
        0.0f,
        attributes));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID ("osrate", 1),
        "Oversampling",
        juce::StringArray { "50x", "100x", "250x", "1000x" },
        0));  // default: 50x

    return layout;
}

// ==============================================================================
// N× anti-imaging FIR (windowed sinc, Blackman window)
// ==============================================================================
void JitterClockSimAudioProcessor::buildFIR (int os)
{
    constexpr double pi = juce::MathConstants<double>::pi;
    int len = os * kTapsPerPhase;

    jassert (len <= kMaxFIRLength);

    for (int i = 0; i < len; ++i)
    {
        double n = i - (len - 1) * 0.5;
        double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (len - 1))
                       + 0.08 * std::cos (4.0 * pi * i / (len - 1));

        double x = pi * n / os;
        double sinc = (std::abs (x) < 1e-12) ? 1.0 : std::sin (x) / x;
        firCoeffs[size_t (i)] = static_cast<float> (sinc * w);
    }

    // Normalize: DC gain = os (compensate zero-stuffing loss)
    float sum = 0.0f;
    for (int i = 0; i < len; ++i)
        sum += firCoeffs[size_t (i)];

    float gain = static_cast<float> (os) / sum;
    for (int i = 0; i < len; ++i)
        firCoeffs[size_t (i)] *= gain;

    // Zero out unused taps
    for (int i = len; i < kMaxFIRLength; ++i)
        firCoeffs[size_t (i)] = 0.0f;
}

// ==============================================================================
void JitterClockSimAudioProcessor::setupIIR (double sampleRate)
{
    // 32nd-order Butterworth lowpass @ 0.9 × Nyquist
    // 16 biquad sections via bilinear transform from analog prototype
    constexpr int N = kIIROrder;
    constexpr double pi = juce::MathConstants<double>::pi;

    const float fc = static_cast<float> (sampleRate * 0.45);
    const double wc = std::tan (pi * fc / sampleRate);  // pre-warp

    for (int k = 0; k < kIIRSections; ++k)
    {
        // Butterworth LHP pole angle: θ = π(2k + N + 1) / 2N
        double angle = pi * (2.0 * k + N + 1.0) / (2.0 * N);
        double sigma = std::cos (angle);
        double omega = std::sin (angle);

        // Scale by pre-warped cutoff
        double pReal = sigma * wc;
        double pImag = omega * wc;

        // Conjugate pair: den = s² - 2·Re(p)·s + |p|²
        double a = 1.0;
        double b = -2.0 * pReal;
        double c = pReal * pReal + pImag * pImag;

        // Bilinear transform s → (z-1)/(z+1):
        //   H(z) = (z+1)² / (A·z² + B·z + C)
        //   where A = a+b+c, B = 2c-2a, C = a-b+c
        double A = a + b + c;
        double B = 2.0 * c - 2.0 * a;
        double C = a - b + c;

        double norm = 1.0 / A;

        // Multiply numerator by c (wc²) for unity DC gain
        juce::dsp::IIR::Coefficients<float> coeffs (
            static_cast<float> (c * norm),
            static_cast<float> (2.0 * c * norm),
            static_cast<float> (c * norm),
            1.0f,
            static_cast<float> (B * norm),
            static_cast<float> (C * norm));

        *iirL[size_t (k)].coefficients = coeffs;
        *iirR[size_t (k)].coefficients = coeffs;
    }
}

// ==============================================================================
// 32-bit LCG random in [0, 1)
// ==============================================================================
inline float JitterClockSimAudioProcessor::randUni()
{
    rng = (rng * 1103515245u + 12345u) & 0x7fffffffu;
    return static_cast<float> (rng) / static_cast<float> (0x7fffffff);
}

// ==============================================================================
// 4-point Catmull-Rom cubic interpolation
// ==============================================================================
float JitterClockSimAudioProcessor::readCubic (const std::vector<float>& buf,
                                               float pos) const
{
    int   base = static_cast<int> (std::floor (pos));
    float frac = pos - static_cast<float> (base);

    int im1 = (base - 1) & kOSMask;
    int i0  =  base      & kOSMask;
    int i1  = (base + 1) & kOSMask;
    int i2  = (base + 2) & kOSMask;

    float vm1 = buf[size_t (im1)];
    float v0  = buf[size_t (i0)];
    float v1  = buf[size_t (i1)];
    float v2  = buf[size_t (i2)];

    float f2 = frac * frac;
    float f3 = f2   * frac;

    return vm1 * (-0.5f * frac + f2 - 0.5f * f3)
         + v0  * ( 1.0f - 2.5f * f2 + 1.5f * f3)
         + v1  * ( 0.5f * frac + 2.0f * f2 - 1.5f * f3)
         + v2  * (-0.5f * f2 + 0.5f * f3);
}

// ==============================================================================
// Polyphase FIR upsample — compute OS output samples for one channel
// ==============================================================================
void JitterClockSimAudioProcessor::upsampleChannel (
    const std::array<float, kInputBufSize>& inBuf,
    std::vector<float>& osBuf,
    int phaseStart)
{
    for (int phase = 0; phase < oversample; ++phase)
    {
        float acc = 0.0f;
        for (int k = 0; k < kTapsPerPhase; ++k)
        {
            int idx = (inPos - 1 - k + kInputBufSize) & kInputMask;
            acc += inBuf[size_t (idx)] * firCoeffs[size_t (phase + oversample * k)];
        }
        osBuf[size_t ((phaseStart + phase) & kOSMask)] = acc;
    }
}

// ==============================================================================
void JitterClockSimAudioProcessor::parameterChanged (const juce::String& paramID,
                                                      float newValue)
{
    if (paramID != "osrate") return;
    if (!prepared.load (std::memory_order_acquire)) return;

    static const int osTable[] = { 50, 100, 250, 1000 };
    int os = osTable[juce::jlimit (0, 3, static_cast<int> (newValue))];

    if (os == oversample) return;

    buildFIR (os);
    oversample = os;
    firLength  = os * kTapsPerPhase;

    // Reset buffers and cursors
    inBufL.fill (0.0f);
    inBufR.fill (0.0f);
    std::fill (osBufL.begin(), osBufL.end(), 0.0f);
    std::fill (osBufR.begin(), osBufR.end(), 0.0f);
    inPos = 0;
    float firDelay = (firLength - 1) * 0.5f;
    wpos = firDelay + static_cast<float> (oversample);
    rpos = wpos - firDelay;
    rng = 1234567;
    setupIIR (currentSampleRate);
}

// ==============================================================================
void JitterClockSimAudioProcessor::prepareToPlay (double sampleRate, int maxSamples)
{
    juce::ignoreUnused (maxSamples);

    currentSampleRate = sampleRate;
    prepared.store (true, std::memory_order_release);

    // Read OS choice
    static const int osTable[] = { 50, 100, 250, 1000 };
    auto* osParam = apvts.getRawParameterValue ("osrate");
    int os = osTable[osParam != nullptr ? juce::jlimit (0, 3, static_cast<int> (osParam->load())) : 3];

    buildFIR (os);
    oversample = os;
    firLength  = os * kTapsPerPhase;

    // Reset buffers
    inBufL.fill (0.0f);
    inBufR.fill (0.0f);
    std::fill (osBufL.begin(), osBufL.end(), 0.0f);
    std::fill (osBufR.begin(), osBufR.end(), 0.0f);

    // Reset cursors
    inPos = 0;
    float firDelay = (firLength - 1) * 0.5f;
    wpos = firDelay + static_cast<float> (oversample);
    rpos = wpos - firDelay;

    // Reset RNG
    rng = 1234567;

    // Setup IIR
    setupIIR (sampleRate);

    // Prepare IIR filters (16 cascaded biquads per channel)
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = 1;
    spec.numChannels      = 1;
    for (int s = 0; s < kIIRSections; ++s)
    {
        iirL[size_t (s)].prepare (spec);
        iirL[size_t (s)].reset();
        iirR[size_t (s)].prepare (spec);
        iirR[size_t (s)].reset();
    }
}

void JitterClockSimAudioProcessor::releaseResources()
{
}

// ==============================================================================
void JitterClockSimAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto* jitterParam = apvts.getRawParameterValue ("jitter");
    float jitterAmt = jitterParam != nullptr ? jitterParam->load() : 0.0f;

    auto* leftIn   = buffer.getReadPointer  (0);
    auto* rightIn  = buffer.getReadPointer  (1);
    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = buffer.getWritePointer (1);

    for (int s = 0; s < numSamples; ++s)
    {
        // Generate one jitter offset per stereo pair (common clock)
        float jitOS = (randUni() * 2.0f - 1.0f) * jitterAmt * oversample;
        currentJitterDisplay.store (jitOS / static_cast<float> (oversample),
                                    std::memory_order_relaxed);

        // ==================== LEFT ====================
        upsampleChannel (inBufL, osBufL, static_cast<int> (std::floor (wpos)));
        inBufL[size_t (inPos)] = leftIn[s];

        float xL = readCubic (osBufL, rpos + jitOS);
        float yL = xL;
        for (int sec = 0; sec < kIIRSections; ++sec)
            yL = iirL[size_t (sec)].processSample (yL);
        leftOut[s] = yL;

        // ==================== RIGHT ====================
        upsampleChannel (inBufR, osBufR, static_cast<int> (std::floor (wpos)));
        inBufR[size_t (inPos)] = rightIn[s];

        float xR = readCubic (osBufR, rpos + jitOS);
        float yR = xR;
        for (int sec = 0; sec < kIIRSections; ++sec)
            yR = iirR[size_t (sec)].processSample (yR);
        rightOut[s] = yR;

        // Advance cursors
        wpos   = std::fmod (wpos + oversample, static_cast<float> (kOSBufSize));
        rpos   = std::fmod (rpos + oversample, static_cast<float> (kOSBufSize));
        inPos  = (inPos + 1) & kInputMask;
    }
}

// ==============================================================================
void JitterClockSimAudioProcessor::getStateInformation (juce::MemoryBlock& data)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, data);
}

void JitterClockSimAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml != nullptr)
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ==============================================================================
juce::AudioProcessorEditor* JitterClockSimAudioProcessor::createEditor()
{
    return new JitterClockSimAudioProcessorEditor (*this);
}

// ==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JitterClockSimAudioProcessor();
}
