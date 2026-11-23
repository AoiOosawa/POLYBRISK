// POLYBRISK DSP ベンチマーク (SPEC §14.3)。JUCE に依存しない。
// 使い方: polybrisk_bench [seconds]
#include "synth/SynthEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
using polybrisk::EngineParams;
using polybrisk::SynthEngine;
using polybrisk::Waveform;

struct Scenario
{
    const char* name;
    Waveform waveform;
    int unison;
    bool oscB;
    int notes;
    bool rhythm = false;        // Trance Gate と Arpeggiator も ON
    bool allFx = false;         // FX 6 種をすべて ON (SPEC §14.3 の基準条件)
    int filterCharacter = -1;   // -1: 既定 (Clean / 全開)。0..2: Analog / Clean / Punch を EG・Drive・Transient 付きで使う
    bool extras = false;        // D-041: LFO (4 行き先)・Sub・Noise・Phaser も ON
};

void run (const Scenario& scenario, double sampleRate, int blockSize, double seconds)
{
    SynthEngine engine;
    engine.prepare (sampleRate);

    EngineParams params;
    for (auto* o : { &params.oscA, &params.oscB })
    {
        o->waveform = scenario.waveform;
        o->unisonVoices = scenario.unison;
        o->level = 1.0f;
        o->retrigger = false;
    }
    params.oscB.enabled = scenario.oscB;
    params.oscB.octave = 1;
    params.amp = { 0.002f, 0.2f, 0.8f, 0.3f };

    if (scenario.filterCharacter >= 0)
    {
        params.filter.character = static_cast<polybrisk::FilterCharacter> (scenario.filterCharacter);
        params.filter.mode = polybrisk::FilterMode::lp24;
        params.filter.cutoffHz = 600.0f;
        params.filter.resonance = 0.4f;
        params.filter.drive = 0.4f;
        params.filter.envAmount = 0.5f;
        params.filter.keyTrack = 0.5f;
        params.filter.velocity = 0.3f;
        params.filterEnv = { 0.002f, 0.15f, 0.3f, 0.2f };
        params.transient = { true, polybrisk::TransientMode::both, 0.5f, 0.02f, 0.6f, 12.0f };
    }
    if (scenario.allFx)
    {
        auto& fx = params.fx;
        fx.drive.enabled = fx.chorus.enabled = fx.eq.enabled = fx.imager.enabled = fx.delay.enabled = fx.reverb.enabled = true;
        fx.drive.drive = 0.4f;
        fx.eq.lowGainDb = -3.0f;
        fx.eq.highGainDb = 3.0f;
        fx.imager.width = 1.3f;
        fx.imager.monoBass = true;
        fx.delay.feedback = 0.45f;
        fx.reverb.type = polybrisk::ReverbType::hall;
        fx.reverb.decaySeconds = 3.0f;
    }

    if (scenario.extras)
    {
        params.lfo.wave = polybrisk::LfoWave::triangle;
        params.lfo.rateHz = 3.0f;
        params.lfo.retrigger = true;
        params.lfo.toCutoff = 0.3f;
        params.lfo.toPulseWidth = 0.5f;
        params.lfo.toPitchCents = 10.0f;
        params.lfo.toAmp = 0.3f;
        params.sub.level = 0.5f;
        params.noise.level = 0.2f;
        params.fx.phaser.enabled = true;
    }

    if (scenario.rhythm)
    {
        params.gate.enabled = true;
        params.gate.pattern.values.fill (1.0f);
        for (std::size_t i = 3; i < params.gate.pattern.values.size(); i += 4)
            params.gate.pattern.values[i] = 0.0f;
        params.arp.enabled = true;
        params.arp.octaves = 2;
        params.arp.rateBeats = 0.125;   // 1/32: ノートの入れ替わりが最も多い
    }

    engine.setParams (params);

    std::vector<float> left (static_cast<std::size_t> (blockSize)), right (static_cast<std::size_t> (blockSize));

    const int totalBlocks = static_cast<int> (seconds * sampleRate / blockSize);
    const int blocksPerHit = std::max (1, static_cast<int> (0.25 * sampleRate / blockSize)); // 16 分音符 @ 60 BPM 相当の連打
    const double blockMicros = 1.0e6 * blockSize / sampleRate;

    std::vector<double> times;
    times.reserve (static_cast<std::size_t> (totalBlocks));
    double checksum = 0.0;
    int chord = 0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        const auto start = std::chrono::steady_clock::now();

        if (b % blocksPerHit == 0)
        {
            // 前のコードを離して次のコードを弾く (Release が重なりスティールも発生する)
            for (int n = 0; n < scenario.notes; ++n)
                engine.noteOff (36 + ((chord + 3) % 4) * 2 + n * 3);

            for (int n = 0; n < scenario.notes; ++n)
                engine.noteOn (36 + (chord % 4) * 2 + n * 3, 0.9f);

            ++chord;
        }

        engine.setParams (params);
        engine.render (left.data(), right.data(), blockSize);

        const auto end = std::chrono::steady_clock::now();
        times.push_back (std::chrono::duration<double, std::micro> (end - start).count());
        checksum += left[0] + right[static_cast<std::size_t> (blockSize - 1)];
    }

    double total = 0.0;
    for (const double t : times)
        total += t;

    std::sort (times.begin(), times.end());
    const double mean = total / static_cast<double> (times.size());
    const double p99 = times[static_cast<std::size_t> (static_cast<double> (times.size()) * 0.99)];
    const double peak = times.back();

    std::printf ("%-34s | %6.2f%% | %7.1f | %7.1f | %7.1f | %6.1f%%\n",
                 scenario.name, 100.0 * mean / blockMicros, mean, p99, peak, 100.0 * peak / blockMicros);

    if (checksum != checksum)
        std::printf ("  !! NaN detected\n");
}
} // namespace

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 20.0;
    const double sampleRate = 48000.0;
    const int blockSize = 128;

    std::printf ("POLYBRISK DSP benchmark: %.0f Hz, %d samples/block (budget %.0f us), %.0f s of audio per scenario\n",
                 sampleRate, blockSize, 1.0e6 * blockSize / sampleRate, seconds);
    std::printf ("%-34s | %7s | %7s | %7s | %7s | %7s\n", "scenario", "avg CPU", "avg us", "p99 us", "peak us", "peak");

    const Scenario scenarios[] = {
        { "1 note, Saw x1, OSC A",            Waveform::saw,      1, false, 1 },
        { "6 notes, SuperSAW x7, OSC A",      Waveform::superSaw, 7, false, 6 },
        { "16 notes, SuperSAW x7, OSC A+B",   Waveform::superSaw, 7, true, 16 }, // SPEC §14.3 の基準条件 (FX は Phase 4)
        { "  + Analog LP24, EG, Transient",    Waveform::superSaw, 7, true, 16, false, false, 0 },
        { "  + Clean LP24, EG, Transient",     Waveform::superSaw, 7, true, 16, false, false, 1 },
        { "  + Punch LP24, EG, Transient",     Waveform::superSaw, 7, true, 16, false, false, 2 },
        { "  + Analog LP24 + ALL 6 FX",        Waveform::superSaw, 7, true, 16, false, true, 0 },    // SPEC §14.3 の基準条件
        { "  + LFO, Sub, Noise, Phaser (7 FX)", Waveform::superSaw, 7, true, 16, false, true, 0, true },
        { "1 note, Saw x1 + ALL 6 FX",         Waveform::saw,      1, false, 1, false, true, -1 },
        { "16 keys, x7 A+B, FX, Gate + Arp",   Waveform::superSaw, 7, true, 16, true, true, 0 },
        { "16 notes, Saw x9, OSC A+B (max)",  Waveform::saw,      9, true, 16 },
        { "16 notes, Pulse x9, OSC A+B",      Waveform::pulse,    9, true, 16 },
        { "16 notes, Triangle x9, OSC A+B",   Waveform::triangle, 9, true, 16 },
        { "16 notes, Sine x9, OSC A+B",       Waveform::sine,     9, true, 16 },
        { "16 notes, Noise, OSC A+B",         Waveform::noise,    1, true, 16 },
    };

    for (const auto& s : scenarios)
        run (s, sampleRate, blockSize, seconds);

    return 0;
}
