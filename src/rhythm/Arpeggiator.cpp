#include "rhythm/Arpeggiator.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

namespace
{
/** 決定的な擬似乱数 (ステップ番号だけで決まるので、ブロック長や分割位置に依存しない)。 */
std::uint32_t hashStep (std::int64_t step) noexcept
{
    auto x = static_cast<std::uint64_t> (step) * 0x9e3779b97f4a7c15ull + 0x632be59bd9b4e019ull;
    x ^= x >> 31;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 29;
    return static_cast<std::uint32_t> (x >> 16);
}
} // namespace

int Arpeggiator::physicalCount() const noexcept
{
    int count = 0;
    for (int i = 0; i < numKeys; ++i)
        count += keys[static_cast<std::size_t> (i)].physical ? 1 : 0;
    return count;
}

void Arpeggiator::stopSounding (ArpSink& sink) noexcept
{
    if (soundingId != 0)
    {
        sink.arpNoteOff (soundingId);
        soundingId = 0;
    }
}

void Arpeggiator::reset (ArpSink& sink) noexcept
{
    stopSounding (sink);
    numKeys = 0;
    pedalDown = false;
    needsSync = true;
    hasStep = false;
    phraseCounter = 0;
}

void Arpeggiator::startPhrase (double position) noexcept
{
    origin = position;
    needsSync = true;
    hasStep = false;
    phraseCounter = 0;
}

void Arpeggiator::setParams (const ArpParams& newParams, ArpSink& sink) noexcept
{
    const bool wasEnabled = params.enabled;
    const bool wasLatched = params.latch;
    params = newParams;

    if (wasEnabled && ! params.enabled)
    {
        // OFF にした時点で生成ノートを終了する。Latch の保持も捨てる (物理的な押鍵だけ覚えておく)
        stopSounding (sink);
        removeReleasedKeys (sink);
        needsSync = true;
        hasStep = false;
    }
    else if (wasLatched && ! params.latch)
    {
        removeReleasedKeys (sink);
    }
}

void Arpeggiator::removeReleasedKeys (ArpSink& sink) noexcept
{
    // 物理的に押されていない鍵を取り除く (ペダルが踏まれていれば残す)
    if (! pedalDown)
    {
        int kept = 0;
        for (int i = 0; i < numKeys; ++i)
            if (keys[static_cast<std::size_t> (i)].physical)
                keys[static_cast<std::size_t> (kept++)] = keys[static_cast<std::size_t> (i)];

        numKeys = kept;
    }

    // 何も残らなければ、次のステップ境界を待たずに止める (追加仕様 §4.3)
    if (numKeys == 0)
    {
        stopSounding (sink);
        hasStep = false;
        needsSync = true;
    }
}

void Arpeggiator::keyDown (int midiNote, float velocity, double position, ArpSink& sink) noexcept
{
    const int note = std::clamp (midiNote, 0, 127);
    const bool anyPhysical = physicalCount() > 0;

    // 何も押していない状態からの押鍵は「新しいフレーズ」。Latch / ペダルで残っていた和音は置き換える。
    if (! anyPhysical)
    {
        if (numKeys > 0)
            stopSounding (sink);

        numKeys = 0;
        startPhrase (position);
    }

    // 同じ音程が Latch / ペダルで残っている場合は、それを押し直したことにする
    for (int i = 0; i < numKeys; ++i)
    {
        auto& k = keys[static_cast<std::size_t> (i)];
        if (k.note == note && ! k.physical)
        {
            k.physical = true;
            k.velocity = velocity;
            return;
        }
    }

    if (numKeys < maxKeys)
        keys[static_cast<std::size_t> (numKeys++)] = { note, velocity, ++orderCounter, true };
}

void Arpeggiator::keyUp (int midiNote, ArpSink& sink) noexcept
{
    // 同音が複数あれば、最も古い物理押鍵を 1 つだけ離す
    int target = -1;
    for (int i = 0; i < numKeys; ++i)
    {
        const auto& k = keys[static_cast<std::size_t> (i)];
        if (k.note == midiNote && k.physical && (target < 0 || k.order < keys[static_cast<std::size_t> (target)].order))
            target = i;
    }

    if (target < 0)
        return;

    keys[static_cast<std::size_t> (target)].physical = false;

    if (! params.latch)
        removeReleasedKeys (sink);
}

void Arpeggiator::setPedal (bool down, ArpSink& sink) noexcept
{
    const bool released = pedalDown && ! down;
    pedalDown = down;

    if (released && ! params.latch)
        removeReleasedKeys (sink);
}

void Arpeggiator::handleDiscontinuity (double position, ArpSink& sink) noexcept
{
    stopSounding (sink);

    // Note On 起点で動いている場合も、ここから数え直す (位置の基準が変わったため)
    origin = position;
    needsSync = true;
    hasStep = false;
}

void Arpeggiator::handleHostStop (double position, ArpSink& sink) noexcept
{
    stopSounding (sink);

    // Latch / ペダルで保持しているだけの音は捨てる。押さえたままの鍵は内部クロックで続ける。
    int kept = 0;
    for (int i = 0; i < numKeys; ++i)
        if (keys[static_cast<std::size_t> (i)].physical)
            keys[static_cast<std::size_t> (kept++)] = keys[static_cast<std::size_t> (i)];

    numKeys = kept;
    startPhrase (position);
}

int Arpeggiator::buildSequence (std::array<int, maxSequence>& notes, std::array<float, maxSequence>& velocities) const noexcept
{
    if (numKeys == 0)
        return 0;

    // 並べ替え: As Played は押した順、それ以外は音程の低い順 (同音は押した順)
    std::array<Key, maxKeys> sorted = keys;
    const bool byOrder = params.mode == ArpMode::asPlayed;

    std::sort (sorted.begin(), sorted.begin() + numKeys, [byOrder] (const Key& a, const Key& b)
    {
        if (byOrder || a.note == b.note)
            return a.order < b.order;

        return a.note < b.note;
    });

    // オクターブ展開。MIDI 0..127 の範囲外はスキップする
    std::array<int, maxSequence> up {};
    std::array<float, maxSequence> upVelocity {};
    int count = 0;
    const int octaves = std::clamp (params.octaves, 1, 4);

    for (int octave = 0; octave < octaves; ++octave)
        for (int i = 0; i < numKeys; ++i)
        {
            const int note = sorted[static_cast<std::size_t> (i)].note + 12 * octave;
            if (note <= 127)
            {
                up[static_cast<std::size_t> (count)] = note;
                upVelocity[static_cast<std::size_t> (count)] = sorted[static_cast<std::size_t> (i)].velocity;
                ++count;
            }
        }

    int length = 0;
    const auto push = [&] (int index)
    {
        if (length >= maxSequence)
            return;     // 配列の外には書かない (上限は Up/Down の最悪値に合わせてあるので、ここには来ない)

        notes[static_cast<std::size_t> (length)] = up[static_cast<std::size_t> (index)];
        velocities[static_cast<std::size_t> (length)] = upVelocity[static_cast<std::size_t> (index)];
        ++length;
    };

    switch (params.mode)
    {
        case ArpMode::down:
            for (int i = count - 1; i >= 0; --i)
                push (i);
            break;

        case ArpMode::upDown:
            // 上昇のあと下降。両端の音は折り返しで重ねて鳴らさない
            for (int i = 0; i < count; ++i)
                push (i);
            for (int i = count - 2; i >= 1; --i)
                push (i);
            break;

        case ArpMode::up:
        case ArpMode::random:
        case ArpMode::asPlayed:
        default:
            for (int i = 0; i < count; ++i)
                push (i);
            break;
    }

    return length;
}

int Arpeggiator::advance (const double* beats, int numSamples, bool hostDriven, double beatsPerSample, ArpSink& sink) noexcept
{
    if (! params.enabled || numKeys == 0)
    {
        stopSounding (sink);
        return numSamples;
    }

    const bool relative = usesOrigin (hostDriven);
    const double rate = std::clamp (params.rateBeats, 1.0 / 64.0, 4.0);
    const double gate = std::clamp (static_cast<double> (params.gate), 0.1, 1.0);

    for (int i = 0; i < numSamples; ++i)
    {
        const double b = relative ? beats[i] - origin : beats[i];
        const auto location = rhythm::locateStep (b, rate, params.swing);

        if (needsSync)
        {
            // フレーズの開始・再同期。ステップの頭にいればすぐ鳴らし、途中なら次の境界を待つ
            needsSync = false;

            if (b - location.start > 2.0 * beatsPerSample)
            {
                hasStep = true;
                currentStep = location.index;
            }
        }

        const bool noteOffDue = soundingId != 0 && b + rhythm::boundaryTolerance >= noteOffBeats;
        // 1 つ前のステップへ戻っただけなら鳴らし直さない: ホストの拍位置が数サンプルだけ手前に補正されると
        // (クロックは 4 サンプルまでをジャンプと見なさない)、境界の直後で前のステップに戻り、同じ音が二度鳴ってしまう
        const bool stepDue = ! hasStep || (location.index != currentStep && location.index != currentStep - 1);

        if (! noteOffDue && ! stepDue)
            continue;

        if (i > 0)
            return i;   // ここまでを先に処理してもらい、次の呼び出しの先頭で発行する

        // 同じサンプルに重なったら Note Off → Note On の順 (追加仕様 §4.4)
        if (noteOffDue || stepDue)
            stopSounding (sink);

        if (stepDue)
        {
            std::array<int, maxSequence> notes {};
            std::array<float, maxSequence> velocities {};
            const int length = buildSequence (notes, velocities);

            hasStep = true;
            currentStep = location.index;

            if (length > 0)
            {
                // Note On 起点ではフレーズ先頭から数え、Transport ではタイムライン上のステップ番号に固定する
                const std::int64_t counter = relative ? phraseCounter : location.index;
                std::int64_t slot = params.mode == ArpMode::random ? static_cast<std::int64_t> (hashStep (counter) % static_cast<std::uint32_t> (length))
                                                                   : counter % length;
                if (slot < 0)
                    slot += length;

                ++phraseCounter;
                if (++idCounter == 0)
                    idCounter = 1;

                const float scaled = velocities[static_cast<std::size_t> (slot)] * std::clamp (params.velocity, 0.25f, 2.0f);
                soundingId = idCounter;
                noteOffBeats = location.start + gate * location.length;
                sink.arpNoteOn (notes[static_cast<std::size_t> (slot)], std::clamp (scaled, 1.0f / 127.0f, 1.0f), soundingId);
            }
        }
    }

    return numSamples;
}

} // namespace polybrisk
