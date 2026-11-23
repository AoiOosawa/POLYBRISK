#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace polybrisk::test
{

/** 立ち上がりゼロクロス間の平均周期から基本周波数 (Hz) を推定する。 */
double estimateFrequency (const float* samples, int numSamples, double sampleRate);

bool allFinite (const juce::AudioBuffer<float>& buffer);
float peakLevel (const juce::AudioBuffer<float>& buffer, int startSample = 0, int numSamples = -1);
double dcOffset (const float* samples, int numSamples);
double rms (const float* samples, int numSamples);
float maxAbsDelta (const float* samples, int numSamples);

/** Blackman-Harris 窓をかけた振幅スペクトル (長さ 2^order / 2 + 1)。 */
std::vector<float> magnitudeSpectrum (const float* samples, int order);

/** 指定周波数の近傍 (±searchBins) の最大振幅。 */
float magnitudeNear (const std::vector<float>& spectrum, double sampleRate, double frequency, int searchBins = 3);

/** 基本波に対する「倍音以外の成分」の最大レベル (dB)。エイリアシングの指標。 */
double worstInharmonicDb (const std::vector<float>& spectrum, double sampleRate, double fundamental,
                          double maxFrequency = 1.0e9);

struct TimedMidi
{
    int samplePosition; // レンダリング全体での絶対位置
    juce::MidiMessage message;
};

/** hostBpm > 0 なら「先頭から再生中のホスト」として、テンポと PPQ 位置をプロセッサーに渡す。
    0 ならホスト情報なし (内部クロック、120 BPM)。 */
/** プロセッサーを blockSizes の順 (繰り返し) で駆動し、totalSamples 分をレンダリングする。
    可変ブロック長のテストでは複数サイズを渡す。 */
juce::AudioBuffer<float> renderProcessor (juce::AudioProcessor& processor,
                                          double sampleRate,
                                          const std::vector<int>& blockSizes,
                                          int totalSamples,
                                          const std::vector<TimedMidi>& events,
                                          double hostBpm = 0.0);

} // namespace polybrisk::test
