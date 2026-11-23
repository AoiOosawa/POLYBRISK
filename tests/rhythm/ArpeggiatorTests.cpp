#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "rhythm/Arpeggiator.h"
#include "state/Parameters.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <vector>

namespace
{
using namespace polybrisk;
using polybrisk::test::TimedMidi;
namespace ids = polybrisk::params::id;

struct Event
{
    int sample;
    bool on;
    int note;
    float velocity;
    std::uint32_t id;
};

/** 生成ノートを記録するだけの送り先。 */
struct Recorder final : ArpSink
{
    void arpNoteOn (int midiNote, float velocity, std::uint32_t noteId) noexcept override
    {
        events.push_back ({ now, true, midiNote, velocity, noteId });
        sounding.push_back (noteId);
    }

    void arpNoteOff (std::uint32_t noteId) noexcept override
    {
        events.push_back ({ now, false, -1, 0.0f, noteId });
        sounding.erase (std::remove (sounding.begin(), sounding.end(), noteId), sounding.end());
    }

    std::vector<int> notes() const
    {
        std::vector<int> result;
        for (const auto& e : events)
            if (e.on)
                result.push_back (e.note);
        return result;
    }

    std::vector<int> onTimes() const
    {
        std::vector<int> result;
        for (const auto& e : events)
            if (e.on)
                result.push_back (e.sample);
        return result;
    }

    int now = 0;
    std::vector<Event> events;
    std::vector<std::uint32_t> sounding;
};

/** Arpeggiator 単体を、エンジンと同じ手順 (peek → advance → 進める) で駆動する。 */
struct ArpHarness
{
    explicit ArpHarness (double sr = 48000.0, double tempo = 120.0) : sampleRate (sr), bpm (tempo)
    {
        clock.prepare (sampleRate);
        clock.setTransport ({ bpm, false, false, 0.0 });
        recorder.events.reserve (4096);
        params.enabled = true;
        apply();
    }

    void apply() { arp.setParams (params, recorder); }
    void down (int note, float velocity = 0.8f) { arp.keyDown (note, velocity, clock.getPosition(), recorder); }
    void up (int note) { arp.keyUp (note, recorder); }
    void pedal (bool isDown) { arp.setPedal (isDown, recorder); }

    /** hostPlaying のときは、ブロックごとにホストの PPQ を渡す。 */
    void run (int numSamples, int block = 128)
    {
        for (int done = 0; done < numSamples;)
        {
            const int blockSize = std::min (block, numSamples - done);

            if (hostPlaying)
                clock.setTransport ({ bpm, true, true, hostPpq });

            if (clock.consumeStopped())
                arp.handleHostStop (clock.getPosition(), recorder);
            if (clock.consumeDiscontinuity())
                arp.handleDiscontinuity (clock.getPosition(), recorder);

            for (int inBlock = 0; inBlock < blockSize;)
            {
                double beats[128];
                const int want = std::min (128, blockSize - inBlock);
                clock.peek (beats, want);
                const int n = arp.advance (beats, want, clock.isHostDriven(), clock.getBeatsPerSample(), recorder);
                clock.advance (n);
                recorder.now += n;
                inBlock += n;
            }

            hostPpq += blockSize * bpm / (60.0 * sampleRate);
            done += blockSize;
        }
    }

    int stepSamples() const { return static_cast<int> (std::lround (params.rateBeats * 60.0 / bpm * sampleRate)); }

    double sampleRate;
    double bpm;
    bool hostPlaying = false;
    double hostPpq = 0.0;
    RhythmClock clock;
    Arpeggiator arp;
    ArpParams params;
    Recorder recorder;
};

std::vector<int> firstNotes (ArpMode mode, int octaves, std::initializer_list<int> chord, int count)
{
    ArpHarness h;
    h.params.mode = mode;
    h.params.octaves = octaves;
    h.apply();
    for (const int note : chord)
        h.down (note);
    h.run (h.stepSamples() * count - 100);
    return h.recorder.notes();
}

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

class ArpeggiatorTests final : public juce::UnitTest
{
public:
    ArpeggiatorTests() : juce::UnitTest ("Arpeggiator", "rhythm") {}

    void expectNotes (const std::vector<int>& actual, const std::vector<int>& expected, const juce::String& what)
    {
        juce::String a, e;
        for (const int n : actual)   a << n << " ";
        for (const int n : expected) e << n << " ";
        expect (actual == expected, what + ": got [" + a.trim() + "], expected [" + e.trim() + "]");
    }

    void runTest() override
    {
        const int c4 = 60, e4 = 64, g4 = 67;

        beginTest ("Modes: Up, Down, Up-Down (ends are not repeated), As Played");
        {
            expectNotes (firstNotes (ArpMode::up, 1, { c4, e4, g4 }, 7), { 60, 64, 67, 60, 64, 67, 60 }, "Up");
            expectNotes (firstNotes (ArpMode::down, 1, { c4, e4, g4 }, 7), { 67, 64, 60, 67, 64, 60, 67 }, "Down");
            expectNotes (firstNotes (ArpMode::upDown, 1, { c4, e4, g4 }, 9), { 60, 64, 67, 64, 60, 64, 67, 64, 60 }, "Up-Down");
            expectNotes (firstNotes (ArpMode::asPlayed, 1, { g4, c4, e4 }, 7), { 67, 60, 64, 67, 60, 64, 67 }, "As Played");

            // 押した順に関係なく、Up は低い音から
            expectNotes (firstNotes (ArpMode::up, 1, { g4, c4, e4 }, 4), { 60, 64, 67, 60 }, "Up (unordered input)");

            // 1 音だけでも設定の Rate で再トリガーする。2 音の Up-Down は交互
            expectNotes (firstNotes (ArpMode::up, 1, { c4 }, 4), { 60, 60, 60, 60 }, "single note");
            expectNotes (firstNotes (ArpMode::upDown, 1, { c4, g4 }, 5), { 60, 67, 60, 67, 60 }, "Up-Down with two notes");
        }

        beginTest ("Octaves: extends upwards; notes above MIDI 127 are skipped");
        {
            expectNotes (firstNotes (ArpMode::up, 2, { c4, e4, g4 }, 7), { 60, 64, 67, 72, 76, 79, 60 }, "Up, 2 octaves");
            expectNotes (firstNotes (ArpMode::down, 2, { c4, e4, g4 }, 7), { 79, 76, 72, 67, 64, 60, 79 }, "Down, 2 octaves");
            expectNotes (firstNotes (ArpMode::up, 4, { c4 }, 5), { 60, 72, 84, 96, 60 }, "Up, 4 octaves");
            expectNotes (firstNotes (ArpMode::up, 3, { 108, 120 }, 5), { 108, 120, 120, 108, 120 }, "skip above 127");   // 108+12=120, 120+12=132 (スキップ), 108+24=132 (スキップ)
            expectNotes (firstNotes (ArpMode::up, 2, { 127 }, 3), { 127, 127, 127 }, "top note only");
        }

        beginTest ("Up-Down with the maximum number of keys and 4 octaves stays inside the sequence buffers (D-068)");
        {
            // 32 鍵 × 4 オクターブ = 128 音。Up-Down は往復で 254 ステップになる (以前は 128 個の配列に書いていた)
            static_assert (Arpeggiator::maxSequence >= 2 * Arpeggiator::maxKeys * 4 - 2);

            ArpHarness h;
            h.params.mode = ArpMode::upDown;
            h.params.octaves = 4;
            h.params.rateBeats = 1.0 / 16.0;
            h.apply();

            for (int i = 0; i < Arpeggiator::maxKeys; ++i)
                h.down (20 + i);

            h.run (h.stepSamples() * 520 - 100);
            const auto notes = h.recorder.notes();
            expect (notes.size() >= 510);

            // 上りの i 番目は「鍵 (i % 32) + 12 × (i / 32)」
            const auto upNote = [] (int i) { return 20 + (i % 32) + 12 * (i / 32); };
            bool ok = notes.size() >= 510;

            for (int i = 0; ok && i < 128; ++i)
                ok = notes[static_cast<size_t> (i)] == upNote (i);                   // 上り
            for (int i = 0; ok && i < 126; ++i)
                ok = notes[static_cast<size_t> (128 + i)] == upNote (126 - i);       // 下り (両端は重ねない)
            for (int i = 0; ok && i < 254; ++i)
                ok = notes[static_cast<size_t> (254 + i)] == notes[static_cast<size_t> (i)];   // 254 ステップで一巡

            expect (ok, "Up-Down sequence with 128 notes is wrong");
        }

        beginTest ("Random: reproducible for the same input, uses only held notes, differs from Up");
        {
            const auto a = firstNotes (ArpMode::random, 2, { c4, e4, g4 }, 64);
            const auto b = firstNotes (ArpMode::random, 2, { c4, e4, g4 }, 64);
            expect (a == b);
            expectEquals (static_cast<int> (a.size()), 64);

            std::vector<int> seen;
            for (const int n : a)
            {
                expect (n == 60 || n == 64 || n == 67 || n == 72 || n == 76 || n == 79);
                if (std::find (seen.begin(), seen.end(), n) == seen.end())
                    seen.push_back (n);
            }
            expectEquals (static_cast<int> (seen.size()), 6);      // 64 ステップあれば全部の音が出る
            expect (a != firstNotes (ArpMode::up, 2, { c4, e4, g4 }, 64));
        }

        beginTest ("Timing: one note per step, sample accurate at every rate, sample rate and block size");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const int rateIndex : { 0, 1, 2, 3, 4, 5 })
            {
                std::vector<std::vector<int>> runs;

                for (const int block : { 32, 128, 512, 2048 })
                {
                    ArpHarness h (rate, 120.0);
                    h.params.rateBeats = polybrisk::params::arpRateBeats (rateIndex);
                    h.apply();
                    h.down (c4);
                    h.down (g4);
                    h.run (static_cast<int> (rate * 2.0), block);
                    runs.push_back (h.recorder.onTimes());

                    const double step = h.params.rateBeats * 60.0 / 120.0 * rate;
                    const auto& times = runs.back();
                    expect (times.size() > 3);

                    for (size_t i = 0; i < times.size(); ++i)
                        expectWithinAbsoluteError (static_cast<double> (times[i]), static_cast<double> (i) * step, 1.01);
                }

                for (size_t r = 1; r < runs.size(); ++r)
                    expect (runs[r] == runs[0]);    // ブロック長に依存しない
            }
        }

        beginTest ("BPM 120: 1/16 steps are 125 ms apart, 1/16T about 83.33 ms");
        {
            ArpHarness h;
            h.down (c4);
            h.run (48000);
            const auto straight = h.recorder.onTimes();
            expectEquals (straight[1] - straight[0], 6000);
            expectEquals (straight[4] - straight[0], 24000);

            ArpHarness t;
            t.params.rateBeats = polybrisk::params::arpRateBeats (5);
            t.apply();
            t.down (c4);
            t.run (48000);
            const auto triplet = t.recorder.onTimes();
            expectWithinAbsoluteError (static_cast<double> (triplet[1] - triplet[0]), 4000.0, 1.0);
            expectEquals (triplet[3] - triplet[0], 12000);      // 3 つで 1/8 = 250 ms
        }

        beginTest ("Gate Length: note-off after that share of the step; 100% gives Off then On on the same sample");
        {
            for (const float gate : { 0.1f, 0.5f, 0.65f })
            {
                ArpHarness h;
                h.params.gate = gate;
                h.apply();
                h.down (c4);
                h.run (30000);

                const auto& e = h.recorder.events;
                expect (e[0].on && ! e[1].on && e[2].on);
                expect (e[1].id == e[0].id);
                expectWithinAbsoluteError (static_cast<double> (e[1].sample - e[0].sample), 6000.0 * gate, 1.01);
            }

            ArpHarness full;
            full.params.gate = 1.0f;
            full.apply();
            full.down (c4);
            full.run (30000);
            const auto& e = full.recorder.events;
            // On(0) / Off(6000) On(6000) / Off(12000) On(12000) ...
            expect (e[0].on && ! e[1].on && e[2].on && ! e[3].on && e[4].on);
            expectEquals (e[1].sample, 6000);
            expectEquals (e[2].sample, 6000);
            expect (e[1].id == e[0].id);    // 同じ音程でも、終了するのは前のノート
            expect (e[2].id != e[0].id);
            expectEquals (static_cast<int> (full.recorder.sounding.size()), 1);
        }

        beginTest ("Swing: even steps are late, each pair still lasts two steps, gate length follows the real step length");
        {
            ArpHarness h;
            h.params.swing = 0.5f;
            h.params.gate = 0.5f;
            h.apply();
            h.down (c4);
            h.run (48000);

            const auto times = h.recorder.onTimes();
            expectEquals (times[1] - times[0], 9000);       // T (1 + 0.5)
            expectEquals (times[2] - times[1], 3000);       // T (1 - 0.5)
            expectEquals (times[2] - times[0], 12000);      // 2T
            expectEquals (times[4] - times[0], 24000);

            const auto& e = h.recorder.events;
            expectWithinAbsoluteError (static_cast<double> (e[1].sample - e[0].sample), 4500.0, 1.01);   // 長いステップの半分
            expectWithinAbsoluteError (static_cast<double> (e[3].sample - e[2].sample), 1500.0, 1.01);   // 短いステップの半分

            ArpHarness even;
            even.down (c4);
            even.run (48000);
            const auto evenTimes = even.recorder.onTimes();
            for (size_t i = 1; i < evenTimes.size(); ++i)
                expectEquals (evenTimes[i] - evenTimes[i - 1], 6000);
        }

        beginTest ("Velocity: input velocity is kept per note and scaled, clamped to 1..127");
        {
            ArpHarness h;
            h.down (c4, 0.5f);
            h.down (e4, 1.0f);
            h.run (12100);
            expectWithinAbsoluteError (h.recorder.events[0].velocity, 0.5f, 1.0e-6f);

            h.params.velocity = 2.0f;
            h.apply();
            h.run (12000);
            float c = 0.0f, e = 0.0f;
            for (const auto& ev : h.recorder.events)
                if (ev.on && ev.sample > 12000)
                    (ev.note == c4 ? c : e) = ev.velocity;
            expectWithinAbsoluteError (c, 1.0f, 1.0e-6f);       // 0.5 x 2
            expectWithinAbsoluteError (e, 1.0f, 1.0e-6f);       // 上限でクランプ

            h.params.velocity = 0.25f;
            h.apply();
            ArpHarness quiet;
            quiet.params.velocity = 0.25f;
            quiet.apply();
            quiet.down (c4, 0.01f);
            quiet.run (100);
            expectWithinAbsoluteError (quiet.recorder.events[0].velocity, 1.0f / 127.0f, 1.0e-6f);   // 下限 (消さない)
        }

        beginTest ("Latch off: releasing all keys stops immediately. Latch on: keeps playing, a new chord replaces the old one");
        {
            ArpHarness h;
            h.down (c4);
            h.down (e4);
            h.run (7000);
            h.up (c4);
            h.run (6000);
            expect (! h.recorder.sounding.empty() || h.arp.getNumKeys() == 1);
            h.up (e4);                                          // ステップの途中で全部離す
            expect (h.recorder.sounding.empty());               // 次の境界を待たずに Note Off
            expect (! h.arp.isSounding());
            const auto count = h.recorder.events.size();
            h.run (48000);
            expectEquals (static_cast<int> (h.recorder.events.size()), static_cast<int> (count));   // もう何も鳴らない

            ArpHarness latch;
            latch.params.latch = true;
            latch.apply();
            latch.down (c4);
            latch.down (e4);
            latch.down (g4);
            latch.run (3000);
            latch.up (c4);
            latch.up (e4);
            latch.up (g4);
            latch.run (36000);
            expectNotes (latch.recorder.notes(), { 60, 64, 67, 60, 64, 67, 60 }, "latched chord keeps going");

            // 新しい和音を押すと置き換わる
            latch.recorder.events.clear();
            latch.down (62);
            latch.down (65);
            latch.run (18000);
            for (const int n : latch.recorder.notes())
                expect (n == 62 || n == 65);
            expect (latch.recorder.notes().size() >= 3);

            // Latch を外すと、押していない音は消えて止まる
            latch.up (62);
            latch.up (65);
            latch.run (6000);
            expect (latch.arp.getNumKeys() == 2);
            latch.params.latch = false;
            latch.apply();
            expectEquals (latch.arp.getNumKeys(), 0);
            expect (latch.recorder.sounding.empty());
        }

        beginTest ("Sustain pedal (CC64) holds the chord without Latch; lifting it drops released keys");
        {
            ArpHarness h;
            h.down (c4);
            h.down (g4);
            h.pedal (true);
            h.run (3000);
            h.up (c4);
            h.up (g4);
            h.run (24000);
            expectEquals (h.arp.getNumKeys(), 2);               // ペダルで保持
            expect (h.recorder.notes().size() >= 4);

            h.pedal (false);
            expectEquals (h.arp.getNumKeys(), 0);
            expect (h.recorder.sounding.empty());

            // ペダルを踏んだまま一部だけ押さえ直し、ペダルを離す → 押さえている鍵だけが残る
            h.down (c4);
            h.down (e4);
            h.pedal (true);
            h.up (e4);
            h.pedal (false);
            expectEquals (h.arp.getNumKeys(), 1);
            h.recorder.events.clear();
            h.run (18000);
            for (const int n : h.recorder.notes())
                expectEquals (n, c4);
        }

        beginTest ("Duplicate notes are tracked separately; unknown note-offs are ignored");
        {
            ArpHarness h;
            h.params.mode = ArpMode::asPlayed;
            h.apply();
            h.down (c4);
            h.down (c4);
            h.down (g4);
            expectEquals (h.arp.getNumKeys(), 3);
            h.run (6000 * 4 - 100);
            expectNotes (h.recorder.notes(), { 60, 60, 67, 60 }, "duplicates in As Played");

            h.up (c4);
            expectEquals (h.arp.getNumKeys(), 2);
            h.up (c4);
            expectEquals (h.arp.getNumKeys(), 1);
            h.up (c4);          // もう押していない
            h.up (99);
            expectEquals (h.arp.getNumKeys(), 1);
            h.up (g4);
            expectEquals (h.arp.getNumKeys(), 0);
            expect (h.recorder.sounding.empty());
        }

        beginTest ("Restart = Note On: a new phrase restarts the pattern; adding notes while holding does not");
        {
            ArpHarness h;
            h.down (c4);
            h.down (e4);
            h.down (g4);
            h.run (6000 + 2500);                // 2 ステップ目の途中
            h.down (72);                        // 押さえたまま追加: 位相はリセットしない
            h.run (6000 * 3);
            const auto times = h.recorder.onTimes();
            for (size_t i = 1; i < times.size(); ++i)
                expectEquals (times[i] - times[i - 1], 6000);

            // 全部離してから押し直す: その瞬間から最初の音で始まる
            for (const int n : { c4, e4, g4, 72 })
                h.up (n);
            h.run (1234);
            h.recorder.events.clear();
            const int restartAt = h.recorder.now;
            h.down (e4);
            h.down (c4);
            h.run (12100);
            expectEquals (h.recorder.events[0].sample, restartAt);
            expectNotes (h.recorder.notes(), { 60, 64, 60 }, "restarted phrase");
        }

        beginTest ("Restart = Transport: notes are quantised to the host grid and the order follows the timeline");
        {
            ArpHarness h;
            h.hostPlaying = true;
            h.hostPpq = 8.0;
            h.params.restartOnNote = false;
            h.apply();
            h.run (2000);                       // 再生中。ステップの途中で押さえる
            h.down (c4);
            h.down (e4);
            h.down (g4);
            h.run (48000);

            const auto times = h.recorder.onTimes();
            expectWithinAbsoluteError (static_cast<double> (times[0]), 6000.0, 1.01);   // 次のグリッドまで待つ
            for (size_t i = 1; i < times.size(); ++i)
                expectWithinAbsoluteError (static_cast<double> (times[i] - times[i - 1]), 6000.0, 1.01);

            // 位置 8 拍 = ステップ 32。次のステップ 33 は 33 % 3 = 0 番目の音から
            expectEquals (h.recorder.notes()[0], 60);
            expectEquals (h.recorder.notes()[1], 64);

            // ちょうどグリッド上で押さえた場合は待たずに鳴る
            ArpHarness onGrid;
            onGrid.hostPlaying = true;
            onGrid.params.restartOnNote = false;
            onGrid.apply();
            onGrid.run (12000);
            onGrid.down (c4);
            onGrid.run (100);
            expectEquals (onGrid.recorder.events[0].sample, 12000);
        }

        beginTest ("Host loop / seek / stop: generated notes are ended, nothing hangs, playback re-syncs");
        {
            ArpHarness h;
            h.hostPlaying = true;
            h.params.restartOnNote = false;
            h.params.gate = 1.0f;
            h.params.latch = true;
            h.apply();
            h.down (c4);
            h.down (g4);
            h.run (15000);
            expectEquals (static_cast<int> (h.recorder.sounding.size()), 1);

            // ループで先頭へ戻る
            h.hostPpq = 0.0;
            h.run (64);
            expect (h.recorder.sounding.size() <= 1);
            h.run (20000);
            expectEquals (static_cast<int> (h.recorder.sounding.size()), 1);

            // 鍵盤を離す (Latch で継続) → ホスト停止: 保持しているだけの音は消える
            h.up (c4);
            h.up (g4);
            h.run (6000);
            expectEquals (h.arp.getNumKeys(), 2);
            h.hostPlaying = false;
            h.clock.setTransport ({ 120.0, false, true, h.hostPpq });
            h.run (64);
            expectEquals (h.arp.getNumKeys(), 0);
            expect (h.recorder.sounding.empty());

            // On と Off の数が合っている (取り残しがない)
            int ons = 0, offs = 0;
            for (const auto& e : h.recorder.events)
                (e.on ? ons : offs) += 1;
            expectEquals (offs, ons);
        }

        beginTest ("Tempo changes keep the notes on the host grid (no drift)");
        {
            ArpHarness h;
            h.hostPlaying = true;
            h.params.restartOnNote = false;
            h.apply();
            h.down (c4);

            std::vector<double> ppqAtOn;
            const double tempos[] = { 120.0, 174.0, 90.0, 133.3 };

            for (int section = 0; section < 4; ++section)
            {
                h.bpm = tempos[section];
                const auto before = h.recorder.events.size();
                const double startPpq = h.hostPpq;
                const int startSample = h.recorder.now;
                h.run (48000, 256);

                for (size_t i = before; i < h.recorder.events.size(); ++i)
                    if (h.recorder.events[i].on)
                        ppqAtOn.push_back (startPpq + (h.recorder.events[i].sample - startSample) * h.bpm / (60.0 * 48000.0));
            }

            expect (ppqAtOn.size() > 20);
            for (const double ppq : ppqAtOn)
            {
                const double steps = ppq / 0.25;
                expectWithinAbsoluteError (steps, std::round (steps), 0.002);   // 16 分のグリッド上 (誤差 1 サンプル程度)
            }
        }

        beginTest ("Engine: arp replaces direct notes, releases cleanly, and leaves no voices behind");
        {
            SynthEngine engine;
            engine.prepare (48000.0);
            EngineParams p;
            p.oscA.waveform = Waveform::sine;
            p.amp = { 0.001f, 0.02f, 1.0f, 0.01f };
            p.arp.enabled = true;
            p.arp.gate = 0.5f;
            engine.setParams (p);

            const auto render = [&] (int n)
            {
                juce::AudioBuffer<float> out (2, n);
                for (int pos = 0; pos < n; pos += 256)
                    engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (256, n - pos));
                return out;
            };

            engine.noteOn (57, 1.0f);   // 220 Hz
            engine.noteOn (69, 1.0f);   // 440 Hz
            const auto out = render (24000);

            // 1 ステップ目 (0〜62 ms) は 220 Hz だけ、2 ステップ目 (125〜187 ms) は 440 Hz だけ
            expectWithinAbsoluteError (polybrisk::test::estimateFrequency (out.getReadPointer (0, 480), 2400, 48000.0), 220.0, 3.0);
            expectWithinAbsoluteError (polybrisk::test::estimateFrequency (out.getReadPointer (0, 6480), 2400, 48000.0), 440.0, 6.0);
            // Gate Length 50%: ステップの後半は無音 (Release 10 ms の後)
            expect (polybrisk::test::peakLevel (out, 4800, 1100) < 1.0e-3f);
            expect (engine.getActiveVoiceCount() <= 2);

            engine.noteOff (57);
            engine.noteOff (69);
            render (4800);
            expectEquals (engine.getActiveVoiceCount(), 0);
            expectEquals (polybrisk::test::peakLevel (render (4800)), 0.0f);

            // 押さえたまま Arp を OFF: 生成ノートが終わり、元のノートが鳴り出すこともない
            engine.noteOn (60, 1.0f);
            render (3000);
            expect (engine.getActiveVoiceCount() > 0);
            p.arp.enabled = false;
            engine.setParams (p);
            render (4800);
            expectEquals (engine.getActiveVoiceCount(), 0);
            engine.noteOff (60);

            // All Notes Off / All Sound Off / reset でも残らない (Latch 中でも)
            p.arp.enabled = true;
            p.arp.latch = true;
            engine.setParams (p);
            engine.noteOn (60, 1.0f);
            engine.noteOff (60);
            render (24000);
            expect (engine.getArpeggiator().getNumKeys() == 1);
            engine.allNotesOff();
            render (4800);
            expectEquals (engine.getActiveVoiceCount(), 0);
            expectEquals (engine.getArpeggiator().getNumKeys(), 0);
            expectEquals (polybrisk::test::peakLevel (render (24000)), 0.0f);

            engine.noteOn (60, 1.0f);
            render (2000);
            engine.allSoundOff();
            render (4800);
            expectEquals (engine.getActiveVoiceCount(), 0);
            expectEquals (engine.getArpeggiator().getNumKeys(), 0);
        }

        beginTest ("Engine: notes played before the arp was switched on still release normally");
        {
            SynthEngine engine;
            engine.prepare (48000.0);
            EngineParams p;
            p.amp = { 0.001f, 0.02f, 1.0f, 0.01f };
            engine.setParams (p);
            engine.noteOn (60, 1.0f);

            float l[512], r[512];
            engine.render (l, r, 512);
            p.arp.enabled = true;
            engine.setParams (p);
            engine.render (l, r, 512);
            expectEquals (engine.getActiveVoiceCount(), 1);   // 直接鳴らした音はそのまま
            engine.noteOff (60);

            for (int i = 0; i < 20; ++i)
                engine.render (l, r, 512);
            expectEquals (engine.getActiveVoiceCount(), 0);
        }

        beginTest ("Engine: a chord held in Poly still releases after switching to Mono or Legato (D-068)");
        {
            for (const auto mode : { VoiceMode::mono, VoiceMode::legato })
            {
                SynthEngine engine;
                engine.prepare (48000.0);
                EngineParams p;
                p.amp = { 0.001f, 0.02f, 1.0f, 0.01f };
                engine.setParams (p);
                engine.noteOn (60, 1.0f);
                engine.noteOn (64, 1.0f);
                engine.noteOn (67, 1.0f);

                float l[512], r[512];
                engine.render (l, r, 512);
                expectEquals (engine.getActiveVoiceCount(), 3);

                p.voiceMode = mode;
                engine.setParams (p);
                engine.render (l, r, 512);

                // 最後に押した鍵 (67) 以外から離す。以前は 60 のボイスが鳴りっぱなしになった
                engine.noteOff (60);
                engine.noteOff (67);
                engine.noteOff (64);

                for (int i = 0; i < 40; ++i)
                    engine.render (l, r, 512);

                expectEquals (engine.getActiveVoiceCount(), 0);
            }
        }

        beginTest ("Processor: arp + gate together through MIDI, every sample rate and block size; nothing hangs");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            std::vector<juce::AudioBuffer<float>> outputs;

            for (const int block : { 32, 128, 512, 2048 })
            {
                PolyBriskAudioProcessor processor;
                setParam (processor, ids::arpEnabled, 1.0f);
                setParam (processor, ids::arpOctaves, 2.0f);
                setParam (processor, ids::gateEnabled, 1.0f);
                setParam (processor, ids::gateRate, 2.0f);          // 1/32
                setParam (processor, ids::gateRestart, 1.0f);       // Note On (ホスト情報なしでも再現できる)
                setParam (processor, ids::oscA.wave, 5.0f);
                setParam (processor, ids::filterCharacter, 1.0f);
                setParam (processor, ids::ampRelease, 5.0f);

                const int total = static_cast<int> (rate * 3.0);
                std::vector<TimedMidi> events;
                for (const int note : { 60, 64, 67 })
                    events.push_back ({ 1000, juce::MidiMessage::noteOn (1, note, 0.9f) });
                events.push_back ({ static_cast<int> (rate * 1.0), juce::MidiMessage::noteOn (1, 64, 0.0f) });      // Velocity 0 = Note Off
                events.push_back ({ static_cast<int> (rate * 1.5), juce::MidiMessage::noteOff (1, 60) });
                events.push_back ({ static_cast<int> (rate * 1.5), juce::MidiMessage::noteOff (1, 67) });

                const auto out = polybrisk::test::renderProcessor (processor, rate, { block }, total, events);
                expect (polybrisk::test::allFinite (out));
                expect (polybrisk::test::peakLevel (out) <= 1.0f);
                expect (polybrisk::test::peakLevel (out, 1000, static_cast<int> (rate)) > 0.02f);
                expectEquals (polybrisk::test::peakLevel (out, static_cast<int> (rate * 2.0), static_cast<int> (rate)), 0.0f);  // 全部離した後は無音
                outputs.push_back (out);
            }

            // ブロック長を変えても同じ音
            for (size_t i = 1; i < outputs.size(); ++i)
            {
                float worst = 0.0f;
                for (int s = 0; s < outputs[0].getNumSamples(); ++s)
                    worst = std::max (worst, std::abs (outputs[i].getSample (0, s) - outputs[0].getSample (0, s)));
                expect (worst < 1.0e-4f, "block-size dependence " + juce::String (worst));
            }
        }
    }
};

ArpeggiatorTests arpeggiatorTests;
} // namespace
