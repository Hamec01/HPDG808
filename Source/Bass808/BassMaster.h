#pragma once

#include <array>
#include <vector>

#include "Bass808/BassEngine.h"
#include "Bass808/BassSettings.h"

namespace bbg
{
// HPDG 808's MASTER: the bass made ready for a mix, after the mono engine (mono in, stereo out, no latency).
//
//   SUB FILTER   what lies under the note's weight: Cut (24 dB / octave high-pass) or Boost (a 12 dB high-pass
//                with a +6 dB bump at its frequency: the rumble goes, the sub gets heavier)
//   DYNAMICS     Amount < 0 compresses what is over the threshold (even notes), > 0 pushes down what is
//                under it (shorter, tighter tails)
//   BAND         everything under FREQ stays in the middle, its level set by GAIN; WIDTH spreads only what
//                is above FREQ (side = that band delayed and diffused; left + right is the mono sound again)
//   HARMONICS    harmonics made from the band under FREQ (2nd / 3rd, even / odd, crunchy), sent to the
//                middle (heard on small speakers) or the sides (a wide bass with a mono sub)
//   LIMIT        a soft ceiling at -0.2 dBFS
//
// With every control at its default the output is the input, sample for sample, on both channels.
// Only the HPDG 808 plugin runs it: HPDG's Sub808 lane stays mono and as it was.
class BassMaster
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    // left / right must not be `mono`.
    void process(const float* mono, float* left, float* right, int numSamples, const BassSettings&) noexcept;

    static bool isNeutral(const BassSettings&) noexcept;
    // For the display and the lab: the level change at `hz` of the sub filter and the band's gain (dB),
    // and the dynamics' gain (dB) for a steady level.
    static double responseDb(const BassSettings&, double hz) noexcept;
    static double dynamicsGainDb(const BassSettings&, double levelDb) noexcept;

    // Meters (written by process): left / right correlation (1: mono) and the dynamics' gain now, dB.
    float correlation() const noexcept { return meterCorrelation; }
    float gainChangeDb() const noexcept { return meterGainDb; }

    static constexpr double kCeiling = 0.98;

private:
    using Svf = BassVoice::Svf;
    struct AllPass
    {
        std::vector<float> buffer;
        int position = 0;
        double process(double x, double g) noexcept;
    };

    double sampleRate = 48000.0;
    bool idle = true;
    float lastInput = 0.0f; // the input's last sample: from silence the controls are set, not glided
    int neutralFor = 0;

    // smoothed controls
    double subMix = 0.0, subCut = 1.0, subHz = 30.0, freq = 150.0, lowGain = 1.0, width = 0.0;
    double harmonics = 0.0, harmSide = 0.5, dampenHz = 2000.0, limitMix = 0.0, dynMix = 0.0;

    std::array<Svf, 2> subStages {}, lowStages {}, highStages {};
    Svf dampen;
    // dynamics
    double dynEnvelope = 0.0, dynGain = 1.0;
    BassSettings dynSettings; // the dynamics' settings while they were on: switched off, they fade out as they were
    int dynHold = 0;
    // harmonics
    double harmEnvelope = 0.0, harmDcIn = 0.0, harmDcOut = 0.0;
    int harmHold = 0;
    // width
    std::vector<float> delay;
    int delayPosition = 0;
    std::array<AllPass, 2> diffusers;
    // meters
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    float meterCorrelation = 1.0f, meterGainDb = 0.0f;
};
} // namespace bbg
