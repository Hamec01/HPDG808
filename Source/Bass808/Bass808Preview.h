#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "BassDrawWave.h"

namespace bbg
{
// The transport reads a finished render of the current patch. Reference files never enter this path.
class Bass808Preview
{
public:
    static constexpr double kRate = 48000.0;
    using Clip = juce::AudioBuffer<float>;

    enum Mode { Stopped, Playing, Scrubbing };
    void setClip(std::shared_ptr<const Clip>); // message thread
    void setPosition(Mode, double seconds) noexcept; // message thread
    double position() const noexcept { return playedSeconds.load(std::memory_order_relaxed); }
    void mix(juce::AudioBuffer<float>&, double outputRate) noexcept; // audio thread

private:
    std::shared_ptr<const Clip> published, activeClip;
    std::vector<std::shared_ptr<const Clip>> retired;
    std::atomic<std::uint64_t> request { 0 };
    std::atomic<float> playedSeconds { 0.0f };
    std::uint64_t lastRequest = 0;
    Mode mode = Stopped;
    double seconds = 0.0, scrubEnd = 0.0;
    int fadeLeft = 0;
    float lastLeft = 0.0f, lastRight = 0.0f, fromLeft = 0.0f, fromRight = 0.0f;
};

// A visual-only copy of the first ten seconds, independent of the synth and Sample layer.
struct Bass808Reference
{
    static constexpr double kMaxSeconds = 10.0;
    static constexpr double kMaxRate = 48000.0;
    std::vector<float> points;
    double rate = kMaxRate;
    juce::String name;
    bool truncated = false;

    double duration() const noexcept { return static_cast<double>(points.size()) / rate; }
    static std::shared_ptr<const Bass808Reference> load(const juce::File&);
    juce::ValueTree toTree() const;
    static std::shared_ptr<const Bass808Reference> fromTree(const juce::ValueTree&);
    struct Cycle { BassDrawWave::Points points {}; double start = 0.0, seconds = 0.0; };
    bool findCycle(double start, double window, Cycle&) const;
    struct Envelope { float attackMs = 2.0f, holdSeconds = 0.0f, decaySeconds = 0.1f; };
    bool fitEnvelope(Envelope&) const;
    std::shared_ptr<const BassDrawWave> resynthesize() const;
};
} // namespace bbg
