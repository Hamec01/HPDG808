#include "BassMaster.h"

#include <cmath>
#include <complex>

namespace bbg
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kButterA = 1.0 / 0.54119610, kButterB = 1.0 / 1.30656296; // 4th-order Butterworth, as two sections
constexpr double kBoostK = 0.5;                                            // Q 2: +6 dB at the frequency
constexpr int kControl = 16;                                               // samples per control step

double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }

double softCeiling(double x) noexcept
{
    constexpr double knee = 0.7, room = BassMaster::kCeiling - knee;
    const double a = std::abs(x);
    if (a <= knee)
        return x;
    return std::copysign(knee + room * std::tanh((a - knee) / room), x);
}
} // namespace

double BassMaster::AllPass::process(double x, double g) noexcept
{
    const double delayed = buffer[static_cast<size_t>(position)];
    const double y = delayed - g * x;
    buffer[static_cast<size_t>(position)] = static_cast<float>(x + g * y);
    if (++position >= static_cast<int>(buffer.size()))
        position = 0;
    return y;
}

bool BassMaster::isNeutral(const BassSettings& s) noexcept
{
    return s.masterSub == 0 && std::abs(s.masterLowGainDb) < 0.005f && s.masterWidth < 0.05f && s.masterHarmonics < 0.0005f
        && std::abs(s.masterDynamics) < 0.0005f && !s.masterLimit;
}

double BassMaster::dynamicsGainDb(const BassSettings& s, double levelDb) noexcept
{
    const double amount = juce::jlimit(-1.0, 1.0, static_cast<double>(s.masterDynamics));
    const double threshold = juce::jlimit(-60.0, 0.0, static_cast<double>(s.masterThresholdDb));
    if (amount < 0.0)
    {
        // compressor, up to 4 : 1, a 6 dB knee, half of the threshold's reduction given back
        const double slope = 1.0 - 1.0 / (1.0 + 3.0 * -amount);
        const double over = levelDb - threshold;
        const double knee = 6.0;
        const double reduced = over <= -knee * 0.5 ? 0.0 : (over >= knee * 0.5 ? over : (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee));
        return slope * (-reduced - 0.5 * threshold);
    }
    if (amount > 0.0)
    {
        // downward expander, up to 1 : 2, no deeper than -36 dB
        const double under = threshold - levelDb;
        return under <= 0.0 ? 0.0 : -std::min(36.0, under * amount);
    }
    return 0.0;
}

double BassMaster::responseDb(const BassSettings& s, double hz) noexcept
{
    using C = std::complex<double>;
    const auto section = [](C p, double k) { return 1.0 / (p * p + k * p + 1.0); };
    C h(1.0, 0.0);
    if (s.masterSub != 0)
    {
        const C p(0.0, hz / juce::jlimit(10.0, 150.0, static_cast<double>(s.masterSubHz)));
        h = s.masterSub == 1 ? p * p * section(p, kButterA) * p * p * section(p, kButterB) : p * p * section(p, kBoostK);
    }
    const C q(0.0, hz / juce::jlimit(40.0, 2500.0, static_cast<double>(s.masterFreqHz)));
    h *= 1.0 + (dbToGain(s.masterLowGainDb) - 1.0) * section(q, kButterA) * section(q, kButterB);
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(h)));
}

void BassMaster::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    delay.assign(static_cast<size_t>(0.0073 * sampleRate) + 1, 0.0f);
    diffusers[0].buffer.assign(static_cast<size_t>(0.0021 * sampleRate) + 1, 0.0f);
    diffusers[1].buffer.assign(static_cast<size_t>(0.0034 * sampleRate) + 1, 0.0f);
    reset();
}

void BassMaster::reset() noexcept
{
    for (auto* stages : { &subStages, &lowStages, &highStages })
        for (auto& stage : *stages)
            stage.clear();
    dampen.clear();
    dynEnvelope = 0.0;
    dynGain = 1.0;
    dynHold = harmHold = 0;
    harmEnvelope = harmDcIn = harmDcOut = 0.0;
    std::fill(delay.begin(), delay.end(), 0.0f);
    delayPosition = 0;
    for (auto& d : diffusers)
    {
        std::fill(d.buffer.begin(), d.buffer.end(), 0.0f);
        d.position = 0;
    }
    subMix = harmonics = width = limitMix = dynMix = 0.0;
    lowGain = 1.0;
    idle = true;
    neutralFor = 0;
    lastInput = 0.0f;
}

void BassMaster::process(const float* mono, float* left, float* right, int n, const BassSettings& s) noexcept
{
    const bool neutral = isNeutral(s);
    if (neutral && idle)
    {
        std::copy(mono, mono + n, left);
        std::copy(mono, mono + n, right);
        meterCorrelation = 1.0f;
        meterGainDb = 0.0f;
        if (n > 0)
            lastInput = mono[n - 1];
        return;
    }

    // targets
    const double tSubMix = s.masterSub != 0 ? 1.0 : 0.0;
    const double tSubCut = s.masterSub == 2 ? 0.0 : 1.0;
    const double tSubHz = juce::jlimit(10.0, 150.0, static_cast<double>(s.masterSubHz));
    const double tFreq = juce::jlimit(40.0, 2500.0, static_cast<double>(s.masterFreqHz));
    const double tLowGain = dbToGain(juce::jlimit(-10.0f, 10.0f, s.masterLowGainDb));
    const double tWidth = juce::jlimit(0.0, 2.0, s.masterWidth / 100.0) * 0.7;
    const double tHarm = juce::jlimit(0.0, 1.0, static_cast<double>(s.masterHarmonics));
    const double tSide = juce::jlimit(0.0, 1.0, static_cast<double>(s.masterHarmSide));
    const double tDampen = juce::jlimit(200.0, 8000.0, static_cast<double>(s.masterDampenHz));
    const double tLimit = s.masterLimit ? 1.0 : 0.0;
    const double tDyn = std::abs(s.masterDynamics) < 0.0005f ? 0.0 : 1.0;
    const int type = juce::jlimit(0, 2, s.masterHarmType);
    if (tDyn > 0.0)
        dynSettings = s;

    if (idle)
    {
        // the first block with the master on: the controls that have no "off" start where they are set;
        // from silence (a project's start, a preset chosen between notes) so do all the others, so the
        // first note already has its limit and its sub filter. Under a sounding note they glide in.
        subHz = tSubHz;
        subCut = tSubCut;
        freq = tFreq;
        harmSide = tSide;
        dampenHz = tDampen;
        if (std::abs(lastInput) < 1.0e-5f)
        {
            subMix = tSubMix;
            lowGain = tLowGain;
            width = tWidth;
            harmonics = tHarm;
            limitMix = tLimit;
            dynMix = tDyn;
            dynGain = dbToGain(dynamicsGainDb(s, -200.0) * (s.masterDynamics < 0.0f ? 1.0 : 0.0)); // the compressor's make-up
        }
        idle = false;
    }
    if (n > 0)
        lastInput = mono[n - 1];

    const double control = 1.0 - std::exp(-kControl / (0.015 * sampleRate)); // 15 ms
    const double perSample = 1.0 - std::exp(-1.0 / (0.015 * sampleRate));
    const double dynAttack = 1.0 - std::exp(-1.0 / (0.003 * sampleRate));
    const double dynRelease = std::exp(-1.0 / (juce::jlimit(20.0, 1000.0, static_cast<double>(s.masterReleaseMs)) * 0.001 * sampleRate));
    const double gainGlide = 1.0 - std::exp(-1.0 / (0.004 * sampleRate));
    const int dynHoldSamples = static_cast<int>(0.022 * sampleRate); // a period of 45 Hz: the gain does not ride the wave
    const double harmRelease = std::exp(-1.0 / (0.03 * sampleRate));
    const int harmHoldSamples = static_cast<int>(0.030 * sampleRate);
    const double dcCoefficient = 1.0 - 2.0 * kPi * 15.0 / sampleRate;
    const double meterKeep = std::exp(-1.0 / (0.1 * sampleRate));
    const int delaySize = static_cast<int>(delay.size());

    for (int start = 0; start < n; start += kControl)
    {
        const int end = std::min(n, start + kControl);
        const auto glide = [control](double& value, double target) { value += control * (target - value); };
        // the filters' frequencies move here, every 16 samples; the levels and mixes glide per sample
        // (a level stepped every 16 samples under a held bass is a staircase: it buzzes)
        glide(subHz, tSubHz);
        glide(freq, tFreq);
        glide(dampenHz, tDampen);

        const auto gOf = [this](double hz) { return std::tan(kPi * std::min(hz, 0.45 * sampleRate) / sampleRate); };
        const double gSub = gOf(subHz), gFreq = gOf(freq), gDampen = gOf(dampenHz);
        const bool subOn = subMix > 1.0e-4 || tSubMix > 0.0;
        const bool harmOn = harmonics > 1.0e-5 || tHarm > 0.0;
        const bool widthOn = width > 1.0e-5 || tWidth > 0.0;
        const bool dynOn = dynMix > 1.0e-4 || tDyn > 0.0;
        const bool bandOn = harmOn || std::abs(lowGain - 1.0) > 1.0e-5 || std::abs(tLowGain - 1.0) > 1.0e-5;

        for (int i = start; i < end; ++i)
        {
            subMix += perSample * (tSubMix - subMix);
            subCut += perSample * (tSubCut - subCut);
            lowGain += perSample * (tLowGain - lowGain);
            width += perSample * (tWidth - width);
            harmonics += perSample * (tHarm - harmonics);
            harmSide += perSample * (tSide - harmSide);
            limitMix += perSample * (tLimit - limitMix);
            dynMix += perSample * (tDyn - dynMix);
            const double kSubA = kBoostK + subCut * (kButterA - kBoostK);

            double x = mono[i];
            double low = 0.0, band = 0.0, high = 0.0;

            if (subOn)
            {
                subStages[0].tick(x, gSub, kSubA, low, band, high);
                const double a = high;
                subStages[1].tick(a, gSub, kButterB, low, band, high);
                x += subMix * (a + subCut * (high - a) - x);
            }

            if (dynOn)
            {
                const double magnitude = std::abs(x);
                if (magnitude > dynEnvelope)
                {
                    dynEnvelope += dynAttack * (magnitude - dynEnvelope);
                    dynHold = dynHoldSamples;
                }
                else if (dynHold > 0)
                    --dynHold;
                else
                    dynEnvelope *= dynRelease;
                const double levelDb = 20.0 * std::log10(dynEnvelope + 1.0e-9);
                // silence is not "under the threshold": the expander leaves a note's start alone
                const double target = levelDb < -90.0 && dynSettings.masterDynamics > 0.0f ? 1.0 : dbToGain(dynMix * dynamicsGainDb(dynSettings, levelDb));
                dynGain += gainGlide * (target - dynGain);
                x *= dynGain;
            }

            double mid = x, side = 0.0;
            if (bandOn)
            {
                lowStages[0].tick(x, gFreq, kButterA, low, band, high);
                const double first = low;
                lowStages[1].tick(first, gFreq, kButterB, low, band, high);
                const double lowBand = low;
                mid += (lowGain - 1.0) * lowBand;

                if (harmOn)
                {
                    // T_n(x / E): harmonic n of a sine at the level of its envelope E (as the voice's Phantom)
                    const double magnitude = std::abs(lowBand);
                    if (magnitude > harmEnvelope)
                    {
                        harmEnvelope = magnitude;
                        harmHold = harmHoldSamples;
                    }
                    else if (harmHold > 0)
                        --harmHold;
                    else
                        harmEnvelope *= harmRelease;
                    double h = 0.0;
                    if (harmEnvelope > 1.0e-6)
                    {
                        const double u = juce::jlimit(-1.0, 1.0, lowBand / harmEnvelope), u2 = u * u;
                        const double t2 = 2.0 * u2 - 1.0, t3 = u * (4.0 * u2 - 3.0);
                        if (type == 0)
                            h = 0.6 * t2 + 0.4 * t3;
                        else if (type == 1)
                            h = 0.45 * t2 + 0.4 * t3 + 0.3 * (8.0 * u2 * u2 - 8.0 * u2 + 1.0) + 0.25 * u * (16.0 * u2 * u2 - 20.0 * u2 + 5.0);
                        else
                            h = 0.9 * (std::tanh(4.0 * u) / std::tanh(4.0) - u) + 0.25 * t2;
                        h *= harmEnvelope * harmonics;
                    }
                    const double dc = h - harmDcIn + dcCoefficient * harmDcOut;
                    harmDcIn = h;
                    harmDcOut = dc;
                    dampen.tick(dc, gDampen, 1.41421356, low, band, high);
                    mid += (1.0 - harmSide) * low;
                    side += harmSide * low;
                }
            }

            if (widthOn)
            {
                highStages[0].tick(x, gFreq, kButterA, low, band, high);
                const double first = high;
                highStages[1].tick(first, gFreq, kButterB, low, band, high);
                const double delayed = delay[static_cast<size_t>(delayPosition)];
                delay[static_cast<size_t>(delayPosition)] = static_cast<float>(high);
                if (++delayPosition >= delaySize)
                    delayPosition = 0;
                side += width * diffusers[1].process(diffusers[0].process(delayed, 0.6), -0.6);
            }

            double l = mid + side, r = mid - side;
            if (limitMix > 1.0e-4)
            {
                l += limitMix * (softCeiling(l) - l);
                r += limitMix * (softCeiling(r) - r);
            }
            left[i] = static_cast<float>(l);
            right[i] = static_cast<float>(r);
            sumLR = sumLR * meterKeep + l * r;
            sumLL = sumLL * meterKeep + l * l;
            sumRR = sumRR * meterKeep + r * r;
        }
    }
    const double power = std::sqrt(sumLL * sumRR);
    meterCorrelation = power > 1.0e-9 ? static_cast<float>(sumLR / power) : 1.0f;
    meterGainDb = tDyn > 0.0 ? static_cast<float>(20.0 * std::log10(std::max(1.0e-6, dynGain))) : 0.0f;

    // everything back at its default for 100 ms: the states are dropped and the input passes as it is
    neutralFor = neutral ? neutralFor + n : 0;
    if (neutral && neutralFor > static_cast<int>(0.1 * sampleRate))
        reset();
}
} // namespace bbg
