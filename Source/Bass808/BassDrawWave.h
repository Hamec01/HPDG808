#pragma once

#include <array>
#include <memory>
#include <vector>

namespace bbg
{
// An immutable pencil stroke and its octave-spaced, zero-DC playback tables.
class BassDrawWave final
{
public:
    static constexpr int kOrder = 9;
    static constexpr int kSize = 1 << kOrder;
    using Points = std::array<float, kSize>;

    explicit BassDrawWave(const Points&);
    struct Frame { double time = 0.0, hz = 55.0; float gain = 1.0f; Points points {}; };
    BassDrawWave(std::vector<Frame>, double duration, double rootHz);
    const Points& points() const noexcept { return drawn; }
    double read(double phase, double phaseIncrement) const noexcept;
    double read(double phase, double increment, double seconds) const noexcept;
    double pitchRatio(double seconds) const noexcept;
    double duration() const noexcept { return sequenceDuration; }
    double rootHz() const noexcept { return sequenceRoot; }
    const std::vector<Frame>& frames() const noexcept { return sequence; }
    std::shared_ptr<const BassDrawWave> editFrame(size_t, const Points&) const;

private:
    static constexpr int kHighestBand = 7;
    Points drawn;
    std::array<Points, kHighestBand + 1> tables {};
    std::vector<Frame> sequence;
    std::vector<std::shared_ptr<const BassDrawWave>> frameWaves;
    double sequenceDuration = 0.0, sequenceRoot = 55.0;
    size_t frameAt(double seconds) const noexcept;
};
} // namespace bbg
