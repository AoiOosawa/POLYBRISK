// Factory プリセットの試聴・検証ツール。
//   polybrisk_render <output-dir> [sample-rate]   各プリセットを短いフレーズで WAV に書き出す
//   polybrisk_render --levels                     各プリセットの音量を計測し、JSON で出力する (tools/level_presets.py 用)
//   polybrisk_render --screenshots <output-dir>   エディターの画面を PNG に書き出す
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

namespace
{
using polybrisk::test::TimedMidi;
constexpr double demoBpm = 128.0;

struct Phrase
{
    std::vector<TimedMidi> events;
    double lengthSeconds = 0.0;
};

void addChord (Phrase& phrase, double sampleRate, double start, double length, const std::vector<int>& notes, float velocity)
{
    for (const int note : notes)
    {
        phrase.events.push_back ({ static_cast<int> (start * sampleRate), juce::MidiMessage::noteOn (1, note, velocity) });
        phrase.events.push_back ({ static_cast<int> ((start + length) * sampleRate), juce::MidiMessage::noteOff (1, note) });
    }

    phrase.lengthSeconds = std::max (phrase.lengthSeconds, start + length);
}

/** カテゴリごとの試聴フレーズ (SPEC §15「音質の試聴シナリオ」)。128 BPM。 */
Phrase phraseFor (const polybrisk::PresetInfo& info, bool usesRhythm, double sampleRate)
{
    Phrase phrase;
    const double beat = 60.0 / demoBpm;
    const std::vector<std::vector<int>> progression { { 45, 57, 60, 64, 69 }, { 41, 53, 57, 60, 65 }, { 48, 55, 60, 64, 67 }, { 43, 55, 59, 62, 67 } };

    if (info.category == "Bass")
    {
        // 単音のベースライン (和音のルートの 1 オクターブ下)。Arp の音色は 1 音を押さえたままにする。
        // 3 つめと 4 つめの音は重ねて弾き、Legato のグライドが出るようにする
        for (int bar = 0; bar < 4; ++bar)
        {
            const int root = progression[static_cast<size_t> (bar)].front() - 12;
            const double t = bar * 4.0 * beat;

            if (usesRhythm)
            {
                addChord (phrase, sampleRate, t, beat * 3.95, { root }, 0.8f);
                continue;
            }

            addChord (phrase, sampleRate, t,               beat * 0.9,  { root },      1.0f);
            addChord (phrase, sampleRate, t + beat * 1.5,  beat * 0.4,  { root },      0.8f);
            addChord (phrase, sampleRate, t + beat * 2.0,  beat * 0.6,  { root + 12 }, 0.9f);
            addChord (phrase, sampleRate, t + beat * 2.5,  beat * 0.95, { root + 7 },  0.8f);
            addChord (phrase, sampleRate, t + beat * 3.5,  beat * 0.4,  { root },      0.8f);
        }
    }
    else if (usesRhythm || info.category == "SuperSAW" || info.category == "Octave / Layered" || info.category == "Bright Poly"
             || info.category == "Pad / Strings")
    {
        // 4〜5 声の和音を 1 小節ずつ (Am - F - C - G)。Gate / Arp の音色は押さえたままにする
        double t = 0.0;
        for (const auto& chord : progression)
        {
            addChord (phrase, sampleRate, t, beat * (usesRhythm ? 3.95 : 3.75), chord, 0.8f);
            t += beat * 4.0;
        }
    }
    else if (info.category == "Synth Brass")
    {
        // 短音 → レガート気味のコード。後半はベロシティを変える
        const std::vector<int> a { 53, 57, 60, 65 }, b { 55, 58, 62, 67 }, c { 51, 55, 58, 63 };
        double t = 0.0;
        for (int i = 0; i < 4; ++i, t += beat * 0.5)
            addChord (phrase, sampleRate, t, beat * 0.25, i % 2 == 0 ? a : b, 0.9f);
        addChord (phrase, sampleRate, t, beat * 2.0, a, 1.0f);  t += beat * 1.95;
        addChord (phrase, sampleRate, t, beat * 2.0, b, 0.6f);  t += beat * 1.95;
        addChord (phrase, sampleRate, t, beat * 2.0, c, 0.35f); t += beat * 1.95;
        addChord (phrase, sampleRate, t, beat * 3.0, a, 1.0f);
    }
    else if (info.category == "Lead / Hook")
    {
        // 単旋律のフック
        const int melody[] = { 69, 72, 76, 74, 72, 69, 67, 69, 72, 76, 79, 76, 74, 72, 74, 76 };
        for (int i = 0; i < 16; ++i)
            addChord (phrase, sampleRate, i * beat * 0.5, beat * (i % 4 == 3 ? 0.9 : 0.45), { melody[i] }, i % 4 == 0 ? 1.0f : 0.8f);
    }
    else
    {
        // Stab / Pluck: 8 分 / 16 分の反復
        const double pattern[] = { 0.0, 0.75, 1.5, 2.0, 2.5, 2.75, 3.25, 3.5 };
        for (int bar = 0; bar < 4; ++bar)
            for (const double step : pattern)
                addChord (phrase, sampleRate, (bar * 4.0 + step) * beat, beat * 0.2,
                          { progression[static_cast<size_t> (bar)].begin() + 1, progression[static_cast<size_t> (bar)].end() },
                          step == 0.0 ? 1.0f : 0.8f);
    }

    std::stable_sort (phrase.events.begin(), phrase.events.end(),
                      [] (const TimedMidi& x, const TimedMidi& y) { return x.samplePosition < y.samplePosition; });
    phrase.lengthSeconds += 2.5; // Release と残響の余韻
    return phrase;
}

void put (std::vector<unsigned char>& bytes, std::uint32_t value, int count)
{
    for (int i = 0; i < count; ++i)
        bytes.push_back (static_cast<unsigned char> ((value >> (8 * i)) & 0xff));
}

/** 24 bit PCM の WAV を書く。 */
bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const auto channels = static_cast<std::uint32_t> (buffer.getNumChannels());
    const auto frames = static_cast<std::uint32_t> (buffer.getNumSamples());
    const std::uint32_t dataBytes = frames * channels * 3;

    std::vector<unsigned char> bytes;
    bytes.reserve (44 + dataBytes);
    bytes.insert (bytes.end(), { 'R', 'I', 'F', 'F' });
    put (bytes, 36 + dataBytes, 4);
    bytes.insert (bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put (bytes, 16, 4);
    put (bytes, 1, 2);
    put (bytes, channels, 2);
    put (bytes, static_cast<std::uint32_t> (sampleRate), 4);
    put (bytes, static_cast<std::uint32_t> (sampleRate) * channels * 3, 4);
    put (bytes, channels * 3, 2);
    put (bytes, 24, 2);
    bytes.insert (bytes.end(), { 'd', 'a', 't', 'a' });
    put (bytes, dataBytes, 4);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const double clamped = juce::jlimit (-1.0, 1.0, static_cast<double> (buffer.getSample (ch, i)));
            const auto value = static_cast<std::int32_t> (std::lround (clamped * 8388607.0));
            put (bytes, static_cast<std::uint32_t> (value), 3);
        }

    return file.replaceWithData (bytes.data(), bytes.size());
}

struct Rendered
{
    polybrisk::PresetInfo info;
    juce::AudioBuffer<float> audio;
    double peakDb = -200.0;
    double loudnessDb = -200.0;   // 400 ms の窓での最大 RMS (左右の平均)。音量をそろえる目安
};

double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-10)); }

Rendered renderPreset (int index, double sampleRate, const juce::File& scratch)
{
    PolyBriskAudioProcessor processor (scratch);
    auto& manager = processor.getPresetManager();
    manager.load (index);

    Rendered result;
    result.info = manager.getEntry (index).info;

    const auto raw = [&] (const char* id) { return processor.getValueTreeState().getRawParameterValue (id)->load(); };
    const bool usesRhythm = raw ("gate.enabled") >= 0.5f || raw ("arp.enabled") >= 0.5f;

    const auto phrase = phraseFor (result.info, usesRhythm, sampleRate);
    const int totalSamples = static_cast<int> (phrase.lengthSeconds * sampleRate);
    result.audio = polybrisk::test::renderProcessor (processor, sampleRate, { 128 }, totalSamples, phrase.events, demoBpm);
    result.peakDb = toDb (polybrisk::test::peakLevel (result.audio));

    const int window = static_cast<int> (0.4 * sampleRate);
    double best = 0.0;
    for (int start = 0; start + window <= totalSamples; start += window / 4)
    {
        const double l = polybrisk::test::rms (result.audio.getReadPointer (0, start), window);
        const double r = polybrisk::test::rms (result.audio.getReadPointer (1, start), window);
        best = std::max (best, std::sqrt (0.5 * (l * l + r * r)));
    }
    result.loudnessDb = toDb (best);
    return result;
}

int writeScreenshots (const juce::File& outputDir, const juce::File& scratch)
{
    PolyBriskAudioProcessor processor (scratch);
    processor.getPresetManager().load (0);

    const auto save = [&] (juce::Component& component, const juce::String& name)
    {
        const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, 2.0f);
        juce::MemoryOutputStream stream;
        juce::PNGImageFormat png;

        if (! png.writeImageToStream (image, stream) || ! outputDir.getChildFile (name).replaceWithData (stream.getData(), stream.getDataSize()))
        {
            std::cerr << "failed to write " << name << std::endl;
            return false;
        }

        std::cout << outputDir.getChildFile (name).getFullPathName() << std::endl;
        return true;
    };

    bool ok = true;

    processor.getMetaState().setAdvancedView (false);
    {
        PolyBriskAudioProcessorEditor editor (processor);
        ok = ok && save (editor, "polybrisk_simple.png");
    }

    {
        // Standalone の見た目 (画面内鍵盤とホイール)。ラッパーの種類は生成時に決まるので、別のインスタンスを作る
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        PolyBriskAudioProcessor standalone (scratch);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        standalone.getPresetManager().load (0);
        PolyBriskAudioProcessorEditor editor (standalone);
        ok = ok && save (editor, "polybrisk_standalone.png");
    }

    processor.getMetaState().setAdvancedView (true);
    for (int tab = 0; tab < PolyBriskAudioProcessorEditor::numAdvancedTabs; ++tab)
    {
        PolyBriskAudioProcessorEditor editor (processor);
        editor.showAdvancedTab (tab);
        ok = ok && save (editor, "polybrisk_advanced_" + juce::String (tab + 1) + ".png");
    }

    {
        // OSC タブ: 波形が Pulse のとき (Pulse Width のノブが出て、行がいちばん長くなる)
        const auto pulse = [&processor] (const char* parameterId)
        {
            if (auto* parameter = processor.getValueTreeState().getParameter (parameterId))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (polybrisk::Waveform::pulse)));
        };

        pulse (polybrisk::params::id::oscA.wave);
        pulse (polybrisk::params::id::oscB.wave);

        // Phase は文字数の多い Retrigger にしておく
        for (const char* parameterId : { polybrisk::params::id::oscA.phaseMode, polybrisk::params::id::oscB.phaseMode })
            if (auto* parameter = processor.getValueTreeState().getParameter (parameterId))
                parameter->setValueNotifyingHost (1.0f);

        {
            PolyBriskAudioProcessorEditor editor (processor);
            editor.showAdvancedTab (0);
            ok = ok && save (editor, "polybrisk_osc_pulse.png");
        }

        processor.getPresetManager().load (0);
    }

    {
        // Rhythm タブ: ステレオの Gate パターン (上段が左、下段が右)
        const auto set = [&processor] (const char* parameterId, float normalised)
        {
            if (auto* parameter = processor.getValueTreeState().getParameter (parameterId))
                parameter->setValueNotifyingHost (normalised);
        };

        for (size_t i = 0; i < polybrisk::factoryGatePatterns.size(); ++i)
            if (juce::String (polybrisk::factoryGatePatterns[i].name) == "Split Gallop")
                processor.getMetaState().setFactoryGatePattern (static_cast<int> (i));

        set (polybrisk::params::id::gateEnabled, 1.0f);
        set (polybrisk::params::id::gateStereo, 1.0f);

        {
            PolyBriskAudioProcessorEditor editor (processor);
            editor.showAdvancedTab (4);
            ok = ok && save (editor, "polybrisk_rhythm_stereo.png");
        }

        set (polybrisk::params::id::gateEnabled, 0.0f);
        set (polybrisk::params::id::gateStereo, 0.0f);
        processor.getPresetManager().load (0);
    }

    {
        PolyBriskAudioProcessorEditor editor (processor);
        editor.showBrowser (true);
        ok = ok && save (editor, "polybrisk_browser.png");
    }

    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: polybrisk_render <output-dir> [sample-rate] | --levels | --screenshots <output-dir>" << std::endl;
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    // ユーザーのプリセットフォルダーに触れないよう、空の一時フォルダーを使う (Factory だけが見える)
    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("polybrisk_render_" + juce::Uuid().toString());
    const juce::String mode = juce::String::fromUTF8 (argv[1]);
    int exitCode = 0;

    if (mode == "--screenshots")
    {
        const auto outputDir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argc > 2 ? argv[2] : "."));
        exitCode = outputDir.createDirectory() ? writeScreenshots (outputDir, scratch) : 1;
    }
    else
    {
        const bool levelsOnly = mode == "--levels";
        const double sampleRate = (! levelsOnly && argc > 2) ? std::atof (argv[2]) : 48000.0;
        const auto outputDir = juce::File::getCurrentWorkingDirectory().getChildFile (mode);

        if (sampleRate < 8000.0 || (! levelsOnly && ! outputDir.createDirectory()))
        {
            std::cerr << "cannot use output directory or sample rate" << std::endl;
            return 1;
        }

        int count = 0;
        {
            PolyBriskAudioProcessor probe (scratch);
            count = probe.getPresetManager().getNumPresets();
        }

        if (levelsOnly)
            std::printf ("{\n");

        for (int i = 0; i < count; ++i)
        {
            const auto rendered = renderPreset (i, sampleRate, scratch);

            if (levelsOnly)
            {
                std::printf ("  %s: { \"peakDb\": %.2f, \"loudnessDb\": %.2f }%s\n", juce::JSON::toString (juce::var (rendered.info.name)).toRawUTF8(),
                             rendered.peakDb, rendered.loudnessDb, i + 1 < count ? "," : "");
                continue;
            }

            const auto fileName = juce::String (i + 1).paddedLeft ('0', 3) + "_" + juce::File::createLegalFileName (rendered.info.name).replaceCharacter (' ', '_') + ".wav";
            const auto file = outputDir.getChildFile (fileName);

            if (! writeWav (file, rendered.audio, sampleRate))
            {
                std::cerr << "failed to write " << file.getFullPathName() << std::endl;
                exitCode = 1;
                break;
            }

            std::printf ("%-22s %-18s peak %6.1f dBFS  loudness %6.1f dB  %s\n", rendered.info.name.toRawUTF8(), rendered.info.category.toRawUTF8(),
                         rendered.peakDb, rendered.loudnessDb, file.getFileName().toRawUTF8());
        }

        if (levelsOnly)
            std::printf ("}\n");
    }

    scratch.deleteRecursively();
    return exitCode;
}
