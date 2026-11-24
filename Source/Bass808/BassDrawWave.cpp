#include "BassDrawWave.h"

#include <juce_dsp/juce_dsp.h>

namespace bbg
{
BassDrawWave::BassDrawWave(std::vector<Frame> frames, double duration, double root)
    : sequence(std::move(frames)), sequenceDuration(duration), sequenceRoot(root)
{
    drawn = sequence.front().points;
    for (const auto& frame : sequence)
        frameWaves.push_back(std::make_shared<const BassDrawWave>(frame.points));
}

size_t BassDrawWave::frameAt(double seconds) const noexcept
{
    const auto next = std::upper_bound(sequence.begin(), sequence.end(), seconds,
                                      [](double at, const Frame& frame) { return at < frame.time; });
    return next == sequence.begin() ? 0 : static_cast<size_t>(next - sequence.begin() - 1);
}

double BassDrawWave::pitchRatio(double seconds) const noexcept
{
    if (sequence.empty()) return 1.0;
    return sequence[frameAt(seconds)].hz / sequenceRoot;
}

double BassDrawWave::read(double phase, double increment, double seconds) const noexcept
{
    if (sequence.empty()) return read(phase, increment);
    if (seconds < sequence.front().time || seconds >= sequenceDuration) return 0.0;
    const auto i = frameAt(seconds), j = std::min(i + 1, sequence.size() - 1);
    const double t = i == j ? 0.0 : std::clamp((seconds - sequence[i].time) / (sequence[j].time - sequence[i].time), 0.0, 1.0);
    const double a = frameWaves[i]->read(phase, increment) * sequence[i].gain;
    const double b = frameWaves[j]->read(phase, increment) * sequence[j].gain;
    const double fadeIn = std::min(1.0, seconds / 0.001);
    const double fadeOut = std::min(1.0, (sequenceDuration - seconds) / 0.005);
    return (a + t * (b - a)) * fadeIn * fadeOut;
}

std::shared_ptr<const BassDrawWave> BassDrawWave::editFrame(size_t index, const Points& points) const
{
    if (sequence.empty()) return std::make_shared<const BassDrawWave>(points);
    auto copy = std::make_shared<BassDrawWave>(*this);
    index = std::min(index, sequence.size() - 1);
    copy->sequence[index].points = points;
    copy->frameWaves[index] = std::make_shared<const BassDrawWave>(points);
    if (index == 0) copy->drawn = points;
    return copy;
}

BassDrawWave::BassDrawWave(const Points& points)
{
    using Spectrum = std::array<std::complex<float>, kSize>;
    Spectrum time {}, frequency {}, band {};
    for (size_t i = 0; i < drawn.size(); ++i)
    {
        drawn[i] = std::isfinite(points[i]) ? juce::jlimit(-1.0f, 1.0f, points[i]) : 0.0f;
        time[i] = drawn[i];
    }
    if (std::all_of(drawn.begin(), drawn.end(), [this](float point) { return point == drawn.front(); }))
        return;

    juce::dsp::FFT fft(kOrder);
    fft.perform(time.data(), frequency.data(), false);
    float peak = 1.0f;
    int copied = 0;
    for (size_t i = 0; i < tables.size(); ++i)
    {
        const int limit = 1 << i;
        while (copied < limit)
        {
            const auto bin = static_cast<size_t>(++copied);
            band[bin] = frequency[bin];
            band[kSize - bin] = frequency[kSize - bin];
        }
        fft.perform(band.data(), time.data(), true);
        for (size_t j = 0; j < drawn.size(); ++j)
        {
            tables[i][j] = time[j].real();
            peak = std::max(peak, std::abs(tables[i][j]));
        }
    }
    for (auto& table : tables)
        for (float& sample : table)
            sample /= peak;
}

double BassDrawWave::read(double phase, double phaseIncrement) const noexcept
{
    // Keep partials below Nyquist, and crossfade adjacent octave bands as pitch changes.
    constexpr double minIncrement = 0.45 / (1 << (kHighestBand + 1));
    const double available = 0.45 / std::max(phaseIncrement, minIncrement);
    if (available < 1.0)
        return 0.0;
    const int band = std::min(kHighestBand, std::ilogb(available));
    const double blend = std::min(1.0, available / (1 << band) - 1.0);
    const double index = phase * kSize;
    const auto a = static_cast<size_t>(index) & (kSize - 1);
    const auto b = (a + 1) & (kSize - 1);
    const double fraction = index - std::floor(index);
    const auto sample = [&](int tableIndex)
    {
        const auto& table = tables[static_cast<size_t>(tableIndex)];
        return table[a] + fraction * (table[b] - table[a]);
    };
    const double upper = sample(band);
    if (blend == 1.0)
        return upper;
    const double lower = band > 0 ? sample(band - 1) : 0.0;
    return lower + blend * (upper - lower);
}
} // namespace bbg
