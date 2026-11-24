#include "Bass808Preview.h"
#include "BassSample.h"

#include <algorithm>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>

namespace bbg
{
std::shared_ptr<const BassDrawWave> Bass808Reference::resynthesize() const
{
    const double root = BassSample::detectRootHz(points, rate);
    if (root < 25.0 || root > 400.0) return nullptr;
    float globalPeak = 0.0f;
    for (float x : points) globalPeak = std::max(globalPeak, std::abs(x));
    if (globalPeak < 1.0e-5f) return nullptr;
    std::vector<double> crossings;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (points[i] <= 0.0f && points[i + 1] > 0.0f)
            crossings.push_back(i - points[i] / static_cast<double>(points[i + 1] - points[i]));
    if (crossings.size() < 3) return nullptr;
    std::vector<BassDrawWave::Frame> frames;
    double previousPeriod = rate / root;
    for (size_t c = 0; c + 1 < crossings.size();)
    {
        const double a = crossings[c];
        size_t next = c + 1;
        // The attack can have a steep pitch drop. The body stays near the preceding period.
        if (a / rate > 0.10)
        {
            double best = std::abs(crossings[next] - a - previousPeriod);
            for (size_t j = next + 1; j < crossings.size() && crossings[j] - a < previousPeriod * 1.6; ++j)
            {
                const double error = std::abs(crossings[j] - a - previousPeriod);
                if (error < best) { best = error; next = j; }
            }
        }
        const double period = crossings[next] - a;
        if (period < rate / 4000.0 || period > rate / 20.0) { ++c; continue; }
        BassDrawWave::Frame frame;
        frame.time = a / rate;
        frame.hz = rate / period;
        float peak = 0.0f;
        double mean = 0.0;
        for (size_t i = 0; i < frame.points.size(); ++i)
        {
            const double at = a + period * i / frame.points.size();
            const auto x = static_cast<size_t>(at), y = std::min(x + 1, points.size() - 1);
            frame.points[i] = static_cast<float>(points[x] + (at - x) * (points[y] - points[x]));
            mean += frame.points[i] / frame.points.size();
        }
        for (float& x : frame.points) { x -= static_cast<float>(mean); peak = std::max(peak, std::abs(x)); }
        frame.gain = peak / globalPeak;
        if (peak > 1.0e-7f)
            for (float& x : frame.points) x *= 0.9f / peak;
        if (frames.empty() || frame.time - frames.back().time >= duration() / 255.0)
            frames.push_back(frame);
        previousPeriod = period;
        c = next;
    }
    if (frames.size() < 2) return nullptr;
    return std::make_shared<const BassDrawWave>(std::move(frames), duration(), root);
}

bool Bass808Reference::fitEnvelope(Envelope& result) const
{
    if (points.empty() || rate <= 0.0)
        return false;
    // A 20 ms RMS window follows a bass body without mistaking each zero crossing for decay.
    constexpr double hopSeconds = 0.005;
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(rate * hopSeconds));
    const size_t window = std::max<size_t>(1, static_cast<size_t>(rate * 0.020));
    std::vector<double> power;
    double sum = 0.0;
    size_t end = 0;
    for (size_t begin = 0; begin < points.size(); begin += hop)
    {
        const size_t next = std::min(points.size(), begin + window);
        while (end < next) { sum += points[end] * points[end]; ++end; }
        power.push_back(sum / static_cast<double>(window));
        for (size_t i = begin; i < std::min(begin + hop, end); ++i)
            sum -= points[i] * points[i];
    }
    const auto peakAt = static_cast<size_t>(std::max_element(power.begin(), power.end()) - power.begin());
    const double peak = power[peakAt];
    if (peak < 1.0e-10)
        return false;
    size_t audibleEnd = peakAt;
    for (size_t i = peakAt; i < power.size(); ++i)
        if (power[i] > peak * 1.0e-6)
            audibleEnd = i;
    size_t onset = 0;
    while (onset < peakAt && power[onset] < peak * 0.01)
        ++onset;

    double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (size_t i = peakAt + 1; i <= audibleEnd; ++i)
    {
        const double db = 10.0 * std::log10(std::max(1.0e-20, power[i] / peak));
        if (db > -6.0 || db < -48.0)
            continue;
        const double t = (i - peakAt) * hopSeconds;
        ++n; sx += t; sy += db; sxx += t * t; sxy += t * db;
    }
    const double denominator = n * sxx - sx * sx;
    const double slope = denominator > 1.0e-12 ? (n * sxy - sx * sy) / denominator : 0.0;
    // Also bound the fitted tail by the audible extent of the whole recording.
    const double tail = std::max(0.010, (audibleEnd - peakAt + 1) * hopSeconds);
    const double decay = slope < -0.01 ? -6.020599913 / slope : tail / 10.0;
    result.attackMs = static_cast<float>(juce::jlimit(0.1, 20.0, (peakAt - onset) * hopSeconds * 1000.0));
    result.holdSeconds = static_cast<float>(std::min(3.0, peakAt * hopSeconds));
    result.decaySeconds = static_cast<float>(juce::jlimit(0.01, 4.0, std::min(decay, tail / 10.0)));
    return true;
}

bool Bass808Reference::findCycle(double start, double window, Cycle& result) const
{
    if (points.empty() || !std::isfinite(start) || !std::isfinite(window))
        return false;
    start = juce::jlimit(0.0, duration(), start);
    double end = std::min(duration(), start + std::max(window, 0.0));
    if (end - start < 0.16)
    {
        start = std::max(0.0, start - (0.16 - (end - start)) * 0.5);
        end = std::min(duration(), start + 0.16);
        start = std::max(0.0, end - 0.16);
    }
    double bestScore = 0.88;
    double bestStart = 0.0, bestPeriod = 0.0;
    const auto sample = [&](double at)
    {
        const auto a = std::min(points.size() - 1, static_cast<size_t>(std::max(0.0, at)));
        const auto b = std::min(a + 1, points.size() - 1);
        return points[a] + (at - a) * (points[b] - points[a]);
    };
    // Analyze bounded overlapping sections, then score actual adjacent cycles in the source.
    const double section = std::min(0.24, end - start);
    if (section < 0.08)
        return false;
    for (int part = 0; part < 9; ++part)
    {
        const double from = start + (end - start - section) * part / 8.0;
        const auto first = static_cast<size_t>(from * rate);
        const auto last = std::min(points.size(), static_cast<size_t>((from + section) * rate));
        const std::vector<float> segment(points.begin() + first, points.begin() + last);
        const double hz = BassSample::detectRootHz(segment, rate);
        if (hz < 25.0 || hz > 400.0)
            continue;
        const double period = rate / hz;
        std::vector<double> crossings;
        for (size_t i = first; i + 1 < last; ++i)
            if (points[i] <= 0.0f && points[i + 1] > 0.0f)
                crossings.push_back(i - points[i] / static_cast<double>(points[i + 1] - points[i]));
        for (size_t i = 0; i < crossings.size(); ++i)
        {
            const double a = crossings[i];
            for (size_t j = i + 1; j < crossings.size(); ++j)
            {
                const double length = crossings[j] - a;
                if (length > period * 1.08)
                    break;
                if (length < period * 0.92 || a + 2 * length >= last)
                    continue;
                double difference = 0.0, energy = 0.0;
                for (int k = 0; k < 128; ++k)
                {
                    const double x = sample(a + length * k / 128.0);
                    const double y = sample(a + length * (1.0 + k / 128.0));
                    difference += (x - y) * (x - y);
                    energy += x * x + y * y;
                }
                const double score = energy > 1.0e-6 ? 1.0 - difference / energy : 0.0;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestStart = a;
                    bestPeriod = length;
                }
            }
        }
    }
    if (bestPeriod == 0.0)
        return false;
    Cycle cycle;
    double mean = 0.0;
    for (size_t i = 0; i < cycle.points.size(); ++i)
    {
        cycle.points[i] = static_cast<float>(sample(bestStart + bestPeriod * i / cycle.points.size()));
        mean += cycle.points[i] / cycle.points.size();
    }
    double peak = 0.0;
    for (float& x : cycle.points)
    {
        x -= static_cast<float>(mean);
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    }
    if (peak < 1.0e-5)
        return false;
    for (float& x : cycle.points)
        x = static_cast<float>(x * 0.9 / peak);
    cycle.start = bestStart / rate;
    cycle.seconds = bestPeriod / rate;
    result = cycle;
    return true;
}

void Bass808Preview::setClip(std::shared_ptr<const Clip> clip)
{
    if (auto old = std::atomic_exchange(&published, std::move(clip)))
        retired.push_back(std::move(old));
    retired.erase(std::remove_if(retired.begin(), retired.end(), [](const auto& item) { return item.use_count() == 1; }), retired.end());
}

void Bass808Preview::setPosition(Mode next, double at) noexcept
{
    // One atomic command keeps the seek position and its mode together.
    constexpr std::uint64_t revision = std::uint64_t { 1 } << 40;
    const auto sequence = (request.load(std::memory_order_relaxed) + revision) & ~(revision - 1);
    const auto micros = static_cast<std::uint64_t>(juce::jlimit(0.0, Bass808Reference::kMaxSeconds, at) * 1.0e6);
    request.store(sequence | (static_cast<std::uint64_t>(next) << 32) | micros, std::memory_order_release);
}

void Bass808Preview::mix(juce::AudioBuffer<float>& output, double outputRate) noexcept
{
    const auto command = request.load(std::memory_order_acquire);
    if (command == lastRequest && mode == Stopped && fadeLeft == 0)
        return;

    const int fade = std::max(1, static_cast<int>(outputRate * 0.004));
    const auto startFade = [&]
    {
        fromLeft = lastLeft;
        fromRight = lastRight;
        fadeLeft = fade;
    };
    const auto clip = std::atomic_load(&published);
    if (clip != activeClip)
    {
        activeClip = clip;
        startFade();
    }
    const int length = clip != nullptr ? clip->getNumSamples() : 0;
    const double end = length / kRate;
    if (command != lastRequest)
    {
        lastRequest = command;
        mode = static_cast<Mode>((command >> 32) & 0xff);
        seconds = static_cast<double>(command & 0xffffffffu) * 1.0e-6;
        if (mode == Playing && seconds >= end)
            seconds = 0.0;
        scrubEnd = std::min(end, seconds + 0.095);
        startFade();
    }

    const double step = 1.0 / outputRate;
    const auto* left = length > 0 ? clip->getReadPointer(0) : nullptr;
    const auto* right = length > 0 ? clip->getReadPointer(1) : nullptr;
    auto* outL = output.getWritePointer(0);
    auto* outR = output.getNumChannels() > 1 ? output.getWritePointer(1) : nullptr;
    for (int i = 0; i < output.getNumSamples(); ++i)
    {
        if (length == 0 || (mode == Scrubbing && seconds >= std::min(scrubEnd, end)))
            mode = Stopped;
        if (mode == Playing && seconds >= end)
        {
            seconds = 0.0;
            startFade();
        }

        float l = 0.0f, r = 0.0f;
        if (mode != Stopped)
        {
            const double position = seconds * kRate;
            const int a = std::min(length - 1, static_cast<int>(position));
            const int b = std::min(length - 1, a + 1);
            const float fraction = static_cast<float>(position - a);
            const float tail = mode == Scrubbing ? static_cast<float>(std::min(1.0, (scrubEnd - seconds) * outputRate / fade)) : 1.0f;
            l = tail * (left[a] + fraction * (left[b] - left[a]));
            r = tail * (right[a] + fraction * (right[b] - right[a]));
            seconds += step;
        }
        if (fadeLeft > 0)
        {
            const float t = 1.0f - static_cast<float>(std::min(fadeLeft, fade)) / static_cast<float>(fade);
            l = fromLeft + t * (l - fromLeft);
            r = fromRight + t * (r - fromRight);
            --fadeLeft;
        }
        lastLeft = l;
        lastRight = r;
        if (outR != nullptr)
        {
            outL[i] += l;
            outR[i] += r;
        }
        else
            outL[i] += 0.5f * (l + r);
    }
    playedSeconds.store(static_cast<float>(juce::jlimit(0.0, end, seconds)), std::memory_order_relaxed);
    if (mode == Stopped && fadeLeft == 0)
        activeClip.reset();
}

std::shared_ptr<const Bass808Reference> Bass808Reference::load(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0
        || !std::isfinite(reader->sampleRate) || reader->sampleRate < 1000.0 || reader->sampleRate > 768000.0)
        return nullptr;

    const auto limit = static_cast<juce::int64>(reader->sampleRate * kMaxSeconds);
    const int count = static_cast<int>(std::min(limit, reader->lengthInSamples));
    juce::AudioBuffer<float> raw(static_cast<int>(std::min(2u, reader->numChannels)), count);
    if (!reader->read(&raw, 0, count, 0, true, true))
        return nullptr;

    auto reference = std::make_shared<Bass808Reference>();
    reference->name = file.getFileName();
    reference->rate = std::min(kMaxRate, reader->sampleRate);
    reference->truncated = reader->lengthInSamples > limit;
    const double stride = reader->sampleRate / reference->rate;
    reference->points.resize(static_cast<size_t>(std::max(1, static_cast<int>(count / stride))));
    // Choose the larger channel rather than losing an out-of-phase recording in a mono average.
    const int channel = raw.getNumChannels() == 2 && raw.getMagnitude(1, 0, count) > raw.getMagnitude(0, 0, count) ? 1 : 0;
    const auto* source = raw.getReadPointer(channel);
    for (size_t i = 0; i < reference->points.size(); ++i)
    {
        const double at = i * stride;
        const int a = static_cast<int>(at), b = std::min(a + 1, count - 1);
        const auto value = static_cast<float>(source[a] + (at - a) * (source[b] - source[a]));
        reference->points[i] = std::isfinite(value) ? juce::jlimit(-1.0f, 1.0f, value) : 0.0f;
    }
    return reference;
}

juce::ValueTree Bass808Reference::toTree() const
{
    juce::MemoryOutputStream bytes;
    for (float x : points)
        bytes.writeShort(static_cast<short>(juce::roundToInt(x * 32767.0f)));
    juce::ValueTree tree("WaveReference");
    tree.setProperty("samples", bytes.getMemoryBlock().toBase64Encoding(), nullptr);
    tree.setProperty("rate", rate, nullptr);
    tree.setProperty("name", name, nullptr);
    tree.setProperty("truncated", truncated, nullptr);
    return tree;
}

std::shared_ptr<const Bass808Reference> Bass808Reference::fromTree(const juce::ValueTree& tree)
{
    const double rate = static_cast<double>(tree.getProperty("rate", 0.0));
    const auto encoded = tree.getProperty("samples").toString();
    juce::MemoryBlock bytes;
    if (!tree.isValid() || !std::isfinite(rate) || rate < 1000.0 || rate > kMaxRate
        || encoded.isEmpty() || encoded.length() > 1300000 || !bytes.fromBase64Encoding(encoded)
        || bytes.getSize() % 2 != 0 || bytes.getSize() / 2 > static_cast<size_t>(rate * kMaxSeconds))
        return nullptr;
    auto reference = std::make_shared<Bass808Reference>();
    reference->rate = rate;
    reference->name = tree.getProperty("name").toString();
    reference->truncated = static_cast<bool>(tree.getProperty("truncated", false));
    reference->points.resize(bytes.getSize() / 2);
    if (reference->points.empty())
        return nullptr;
    juce::MemoryInputStream input(bytes, false);
    for (float& point : reference->points)
        point = input.readShort() / 32767.0f;
    return reference;
}
} // namespace bbg
