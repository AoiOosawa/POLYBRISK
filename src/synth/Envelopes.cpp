#include "synth/Envelopes.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

namespace
{
constexpr double minus60dBLog = -6.907755278982137; // ln(0.001)
}

AdsrCoefficients AdsrCoefficients::make (const AdsrParams& params, double sampleRate) noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    const double attackSamples = std::max (1.0, static_cast<double> (params.attack) * sr);
    const double decaySamples = std::max (1.0, static_cast<double> (params.decay) * sr);
    const double releaseSamples = std::max (1.0, static_cast<double> (params.release) * sr);

    AdsrCoefficients c;
    c.attackStep = 1.0 / attackSamples;
    c.decayCoeff = std::exp (minus60dBLog / decaySamples);
    c.sustain = std::clamp (static_cast<double> (params.sustain), 0.0, 1.0);
    c.releaseCoeff = std::exp (minus60dBLog / releaseSamples);
    return c;
}

} // namespace polybrisk
