#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

CalmaProcessor::CalmaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& d : displayReduction) d.store (0.0f);
}

APVTS::ParameterLayout CalmaProcessor::createLayout()
{
    using P  = juce::AudioParameterFloat;
    using ID = juce::ParameterID;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto ms  = [] (float v, int) { return juce::String (v, 1) + " ms"; };
    auto hz  = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                                       : juce::String (juce::roundToInt (v)) + " Hz"; };

    p.push_back (std::make_unique<P> (ID { "depth", 1 }, "Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "sens", 1 }, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "width", 1 }, "Width",
        juce::NormalisableRange<float> (0.05f, 0.6f, 0.001f), 0.2f));
    p.push_back (std::make_unique<P> (ID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (1.0f, 200.0f, 0.1f, 0.4f), 10.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<P> (ID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.4f), 120.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<P> (ID { "low", 1 }, "Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 100.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<P> (ID { "high", 1 }, "High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 16000.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<P> (ID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ID { "delta", 1 }, "Delta", false));

    return { p.begin(), p.end() };
}

bool CalmaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void CalmaProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    setLatencySamples (fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

    float sumSq = 0.0f;
    for (int j = 0; j < 4; ++j) { const float w = window[(size_t) (j * hopSize)]; sumSq += w * w; }
    olaNorm = 1.0f / sumSq;

    for (auto& a : inRing)  a.fill (0.0f);
    for (auto& a : outRing) a.fill (0.0f);
    reduction.fill (0.0f);
    smoothed.fill (0.0f);
    pos = 0;
    hopCount = 0;
}

void CalmaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nCh = juce::jmin (buffer.getNumChannels(), maxCh);
    const int n   = buffer.getNumSamples();

    pDepth = apvts.getRawParameterValue ("depth")->load();
    pSens  = apvts.getRawParameterValue ("sens")->load();
    pWidth = apvts.getRawParameterValue ("width")->load();
    pMix   = apvts.getRawParameterValue ("mix")->load();
    pLow   = apvts.getRawParameterValue ("low")->load();
    pHigh  = apvts.getRawParameterValue ("high")->load();
    pDelta = apvts.getRawParameterValue ("delta")->load() > 0.5f;

    const float attMs = apvts.getRawParameterValue ("attack")->load();
    const float relMs = apvts.getRawParameterValue ("release")->load();
    attCoef = std::exp (-(float) hopSize / (float) (fs * attMs * 0.001));
    relCoef = std::exp (-(float) hopSize / (float) (fs * relMs * 0.001));

    float* ptr[maxCh] = { nullptr, nullptr };
    for (int ch = 0; ch < nCh; ++ch) ptr[ch] = buffer.getWritePointer (ch);

    for (int s = 0; s < n; ++s)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x   = ptr[ch][s];
            const float dry = inRing[(size_t) ch][(size_t) pos]; // amostra de ha fftSize amostras
            const float wet = outRing[(size_t) ch][(size_t) pos];
            outRing[(size_t) ch][(size_t) pos] = 0.0f;
            inRing[(size_t) ch][(size_t) pos]  = x;

            ptr[ch][s] = pDelta ? (dry - wet) : (dry + (wet - dry) * pMix);
        }

        pos = (pos + 1) & (fftSize - 1);

        if (++hopCount >= hopSize)
        {
            hopCount = 0;
            processFrame (nCh);
        }
    }
}

void CalmaProcessor::processFrame (int nCh)
{
    constexpr int mask = fftSize - 1;

    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& d = fftData[(size_t) ch];
        for (int i = 0; i < fftSize; ++i)
            d[(size_t) i] = inRing[(size_t) ch][(size_t) ((pos + i) & mask)] * window[(size_t) i];
        std::fill (d.begin() + fftSize, d.end(), 0.0f);
        fft.performRealOnlyForwardTransform (d.data(), true);
    }

    // espectro (potencia media dos canais -> link estereo) em dB
    for (int k = 0; k < numBins; ++k)
    {
        float p = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float re = fftData[(size_t) ch][(size_t) (2 * k)];
            const float im = fftData[(size_t) ch][(size_t) (2 * k + 1)];
            p += re * re + im * im;
        }
        dbSpec[(size_t) k] = 10.0f * std::log10 (p / (float) nCh + 1.0e-12f);
    }

    prefix[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) dbSpec[(size_t) k];

    const float thrDb  = 12.0f * (1.0f - pSens);
    const float factor = pDepth * 1.5f;
    const float maxRed = 24.0f;
    const float binHz  = (float) fs / (float) fftSize;

    for (int k = 0; k < numBins; ++k)
    {
        const int hw = juce::jmax (3, (int) ((float) k * pWidth));
        const int lo = juce::jmax (0, k - hw);
        const int hi = juce::jmin (numBins - 1, k + hw);
        const float env = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));

        const float f = (float) k * binHz;
        float target = 0.0f;
        if (f >= pLow && f <= pHigh)
            target = juce::jmin (maxRed, factor * juce::jmax (0.0f, dbSpec[(size_t) k] - env - thrDb));

        float& r = reduction[(size_t) k];
        const float c = target > r ? attCoef : relCoef;
        r = target + c * (r - target);
    }

    // suavizacao em frequencia (reduz "musical noise")
    for (int k = 0; k < numBins; ++k)
    {
        const float a = reduction[(size_t) juce::jmax (0, k - 1)];
        const float b = reduction[(size_t) k];
        const float c = reduction[(size_t) juce::jmin (numBins - 1, k + 1)];
        smoothed[(size_t) k] = 0.25f * a + 0.5f * b + 0.25f * c;
        displayReduction[(size_t) k].store (smoothed[(size_t) k], std::memory_order_relaxed);
    }

    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& d = fftData[(size_t) ch];
        for (int k = 0; k < numBins; ++k)
        {
            const float g = std::exp (-smoothed[(size_t) k] * 0.11512925f); // 10^(-dB/20)
            d[(size_t) (2 * k)]     *= g;
            d[(size_t) (2 * k + 1)] *= g;
        }
        fft.performRealOnlyInverseTransform (d.data());

        for (int i = 0; i < fftSize; ++i)
            outRing[(size_t) ch][(size_t) ((pos + i) & mask)] += d[(size_t) i] * window[(size_t) i] * olaNorm;
    }
}

juce::AudioProcessorEditor* CalmaProcessor::createEditor() { return new CalmaEditor (*this); }

void CalmaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CalmaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CalmaProcessor(); }
