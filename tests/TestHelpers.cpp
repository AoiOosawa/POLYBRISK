#include "TestHelpers.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace polybrisk::test
{

double estimateFrequency (const float* samples, int numSamples, double sampleRate)
{
    double firstCrossing = -1.0;
    double lastCrossing = -1.0;
    int crossings = 0;

    for (int i = 1; i < numSamples; ++i)
    {
        const float a = samples[i - 1];
        const float b = samples[i];

        if (a < 0.0f && b >= 0.0f)
        {
            const double position = (i - 1) + static_cast<double> (-a) / static_cast<double> (b - a);

            if (firstCrossing < 0.0)
                firstCrossing = position;

            lastCrossing = position;
            ++crossings;
        }
    }

    if (crossings < 2 || lastCrossing <= firstCrossing)
        return 0.0;

    return (crossings - 1) * sampleRate / (lastCrossing - firstCrossing);
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }

    return true;
}

float peakLevel (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (numSamples < 0)
        numSamples = buffer.getNumSamples() - startSample;

    return buffer.getMagnitude (startSample, numSamples);
}

double dcOffset (const float* samples, int numSamples)
{
    double sum = 0.0;

    for (int i = 0; i < numSamples; ++i)
        sum += samples[i];

    return numSamples > 0 ? sum / numSamples : 0.0;
}

double rms (const float* samples, int numSamples)
{
    double sum = 0.0;

    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double> (samples[i]) * samples[i];

    return numSamples > 0 ? std::sqrt (sum / numSamples) : 0.0;
}

float maxAbsDelta (const float* samples, int numSamples)
{
    float m = 0.0f;

    for (int i = 1; i < numSamples; ++i)
        m = std::max (m, std::abs (samples[i] - samples[i - 1]));

    return m;
}

std::vector<float> magnitudeSpectrum (const float* samples, int order)
{
    const auto size = static_cast<size_t> (1) << order;

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::blackmanHarris);

    std::vector<float> data (size * 2, 0.0f);
    std::copy (samples, samples + size, data.begin());
    window.multiplyWithWindowingTable (data.data(), size);
    fft.performFrequencyOnlyForwardTransform (data.data());

    data.resize (size / 2 + 1);
    return data;
}

float magnitudeNear (const std::vector<float>& spectrum, double sampleRate, double frequency, int searchBins)
{
    const double binHz = sampleRate / static_cast<double> ((spectrum.size() - 1) * 2);
    const int centre = static_cast<int> (std::round (frequency / binHz));
    float m = 0.0f;

    for (int b = centre - searchBins; b <= centre + searchBins; ++b)
        if (b >= 0 && b < static_cast<int> (spectrum.size()))
            m = std::max (m, spectrum[static_cast<size_t> (b)]);

    return m;
}

double worstInharmonicDb (const std::vector<float>& spectrum, double sampleRate, double fundamental, double maxFrequency)
{
    const double binHz = sampleRate / static_cast<double> ((spectrum.size() - 1) * 2);
    const float reference = magnitudeNear (spectrum, sampleRate, fundamental);
    const int guard = 8; // Blackman-Harris のメインローブ幅を除外
    float worst = 0.0f;

    for (int b = static_cast<int> (30.0 / binHz); b < static_cast<int> (spectrum.size()); ++b)
    {
        const double f = b * binHz;

        if (f > maxFrequency)
            break;

        const double nearestHarmonic = std::round (f / fundamental) * fundamental;

        if (nearestHarmonic >= fundamental * 0.5 && std::abs (f - nearestHarmonic) <= guard * binHz)
            continue;

        worst = std::max (worst, spectrum[static_cast<size_t> (b)]);
    }

    return juce::Decibels::gainToDecibels (worst / reference, -200.0f);
}

juce::AudioBuffer<float> renderProcessor (juce::AudioProcessor& processor,
                                          double sampleRate,
                                          const std::vector<int>& blockSizes,
                                          int totalSamples,
                                          const std::vector<TimedMidi>& events,
                                          double hostBpm)
{
    // 再生中のホストを模したプレイヘッド
    struct PlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (true);
            info.setPpqPosition (static_cast<double> (samples) * bpm / (60.0 * rate));
            info.setTimeInSamples (samples);
            return info;
        }

        double bpm = 120.0, rate = 48000.0;
        juce::int64 samples = 0;
    };

    PlayHead playHead;
    playHead.bpm = hostBpm;
    playHead.rate = sampleRate;

    if (hostBpm > 0.0)
        processor.setPlayHead (&playHead);

    const int maxBlock = *std::max_element (blockSizes.begin(), blockSizes.end());

    processor.setRateAndBufferSizeDetails (sampleRate, maxBlock);
    processor.prepareToPlay (sampleRate, maxBlock);

    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block (2, maxBlock);
    int position = 0;
    size_t blockIndex = 0;

    while (position < totalSamples)
    {
        const int n = std::min (blockSizes[blockIndex % blockSizes.size()], totalSamples - position);
        ++blockIndex;

        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.samplePosition >= position && e.samplePosition < position + n)
                midi.addEvent (e.message, e.samplePosition - position);

        // ホストと同様に、要求サイズちょうどのバッファ (前回の内容が残っている) を渡す
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        playHead.samples = position;
        processor.processBlock (view, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, position, view, ch, 0, n);

        position += n;
    }

    processor.releaseResources();
    processor.setPlayHead (nullptr);
    return output;
}

} // namespace polybrisk::test
