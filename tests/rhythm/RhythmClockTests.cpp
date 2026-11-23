#include "TestHelpers.h"
#include "rhythm/RhythmClock.h"
#include "state/Parameters.h"

namespace
{
using polybrisk::RhythmClock;
using polybrisk::TransportInfo;
using polybrisk::rhythm::locateStep;

class RhythmClockTests final : public juce::UnitTest
{
public:
    RhythmClockTests() : juce::UnitTest ("Rhythm clock", "rhythm") {}

    void runTest() override
    {
        beginTest ("Step lengths: 1/16 at 120 BPM is 125 ms, 1/16T is 83.33 ms");
        {
            const double secondsPerBeat = 60.0 / 120.0;
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (1) * secondsPerBeat, 0.125, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (4) * secondsPerBeat, 0.0833333, 1.0e-6);
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (0) * secondsPerBeat, 0.25, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (2) * secondsPerBeat, 0.0625, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (3) * secondsPerBeat, 0.1666667, 1.0e-6);
            expectWithinAbsoluteError (polybrisk::params::arpRateBeats (0) * secondsPerBeat, 0.5, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::arpRateBeats (2) * secondsPerBeat, 0.125, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::arpRateBeats (5) * secondsPerBeat, 0.0833333, 1.0e-6);
            expectEquals (polybrisk::params::gateStepCount (0), 8);
            expectEquals (polybrisk::params::gateStepCount (1), 16);
            expectEquals (polybrisk::params::gateStepCount (2), 32);
        }

        beginTest ("locateStep: Swing 0 is perfectly even; indices are continuous, also for negative positions");
        {
            for (int i = -40; i < 40; ++i)
            {
                const auto at = locateStep (i * 0.25 + 0.1, 0.25, 0.0);
                expectEquals (static_cast<int> (at.index), i);
                expectWithinAbsoluteError (at.start, i * 0.25, 1.0e-12);
                expectWithinAbsoluteError (at.length, 0.25, 1.0e-12);
            }

            expectEquals (static_cast<int> (locateStep (0.0, 0.25, 0.0).index), 0);
            expectEquals (static_cast<int> (locateStep (0.2499999, 0.25, 0.0).index), 0);
            expectEquals (static_cast<int> (locateStep (0.25, 0.25, 0.0).index), 1);
            expectEquals (static_cast<int> (locateStep (-0.0000001, 0.25, 0.0).index), -1);
        }

        beginTest ("locateStep: Swing delays every second step and keeps each pair at 2T");
        for (const double swing : { 0.1, 0.25, 0.5 })
        {
            const double t = 0.25;

            for (int pair = -3; pair < 6; ++pair)
            {
                const auto first = locateStep (pair * 2.0 * t + 0.001, t, swing);
                const auto second = locateStep (pair * 2.0 * t + t * (1.0 + swing) + 0.001, t, swing);

                expectEquals (static_cast<int> (first.index), pair * 2);
                expectEquals (static_cast<int> (second.index), pair * 2 + 1);
                expectWithinAbsoluteError (first.length, t * (1.0 + swing), 1.0e-12);
                expectWithinAbsoluteError (second.length, t * (1.0 - swing), 1.0e-12);
                expectWithinAbsoluteError (first.length + second.length, 2.0 * t, 1.0e-12);
                expectWithinAbsoluteError (second.start, first.start + first.length, 1.0e-12);

                // 偶数番目のステップの直前は、まだ前のステップ
                expectEquals (static_cast<int> (locateStep (second.start - 1.0e-6, t, swing).index), pair * 2);
            }
        }

        beginTest ("Clock follows the host while playing and free-runs otherwise");
        {
            RhythmClock clock;
            clock.prepare (48000.0);
            expectWithinAbsoluteError (clock.getBpm(), 120.0, 1.0e-12);   // テンポ不明なら 120 BPM
            expect (! clock.isHostDriven());

            // 停止中: 内部クロックで進む
            clock.setTransport ({ 0.0, false, false, 0.0 });
            clock.advance (48000);
            expectWithinAbsoluteError (clock.getPosition(), 2.0, 1.0e-9);
            expect (! clock.consumeDiscontinuity());

            // 再生開始: ホストの位置へ移る (不連続)
            clock.setTransport ({ 150.0, true, true, 16.0 });
            expect (clock.isHostDriven());
            expect (clock.consumeDiscontinuity());
            expect (! clock.consumeDiscontinuity());
            expectWithinAbsoluteError (clock.getPosition(), 16.0, 1.0e-12);

            double beats[4];
            clock.peek (beats, 4);
            expectWithinAbsoluteError (beats[1] - beats[0], 150.0 / (60.0 * 48000.0), 1.0e-15);
            clock.advance (512);

            // 連続したブロック: 不連続にならない
            clock.setTransport ({ 150.0, true, true, 16.0 + 512.0 * 150.0 / (60.0 * 48000.0) });
            expect (! clock.consumeDiscontinuity());

            // ループ / シーク
            clock.setTransport ({ 150.0, true, true, 4.0 });
            expect (clock.consumeDiscontinuity());

            // 停止
            clock.advance (256);
            clock.setTransport ({ 150.0, false, true, 4.0 });
            expect (clock.consumeStopped());
            expect (! clock.consumeStopped());
            expect (clock.consumeDiscontinuity());
            expect (! clock.isHostDriven());

            // 不正なテンポはフォールバック
            clock.setTransport ({ std::nan (""), false, false, 0.0 });
            expectWithinAbsoluteError (clock.getBpm(), 120.0, 1.0e-12);
            clock.setTransport ({ -5.0, false, false, 0.0 });
            expectWithinAbsoluteError (clock.getBpm(), 120.0, 1.0e-12);
        }
    }
};

RhythmClockTests rhythmClockTests;
} // namespace
