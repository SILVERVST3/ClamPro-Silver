#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// CALMA - supressor dinamico de ressonancias (STFT, 2048 pts, 75% overlap)
class CalmaProcessor : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1;
    static constexpr int maxCh    = 2;

    CalmaProcessor();
    ~CalmaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CALMA"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // reducao (dB, positiva) por bin, lida pela interface
    std::array<std::atomic<float>, numBins> displayReduction;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processFrame (int nCh);

    juce::dsp::FFT fft { fftOrder };

    std::array<std::array<float, fftSize>, maxCh>     inRing {};
    std::array<std::array<float, fftSize>, maxCh>     outRing {};
    std::array<std::array<float, 2 * fftSize>, maxCh> fftData {};
    std::array<float, fftSize> window {};
    std::array<float, numBins> reduction {}, smoothed {}, dbSpec {};
    std::array<double, numBins + 1> prefix {};

    float olaNorm = 1.0f;
    int pos = 0, hopCount = 0;
    double fs = 44100.0;

    // parametros lidos a cada bloco
    float pDepth = 0.5f, pSens = 0.5f, pWidth = 0.2f, pMix = 1.0f;
    float pLow = 20.0f, pHigh = 20000.0f;
    float attCoef = 0.0f, relCoef = 0.0f;
    bool  pDelta = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalmaProcessor)
};
