// The "Memphis Trap - M808" presets against their specification (--memphis-check): pitch, H2-H5, DC, peak, sample
// rates, block sizes, gate, retrigger, glide, clicks, aliasing, envelope and CPU.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "Bass808/BassEngine.h"

namespace
{
using namespace bbg;
constexpr double kPi2 = 2.0 * juce::MathConstants<double>::pi;

struct Event { double seconds; int note; bool on; };

std::vector<float> play(const BassSettings& s, const std::vector<Event>& events, double seconds, double rate, int block)
{
    BassEngine engine;
    engine.prepare(rate, block);
    engine.setSettings(s);
    const size_t latency = static_cast<size_t>(engine.latencySamples());
    const size_t total = static_cast<size_t>(seconds * rate) + latency;
    std::vector<float> out(total, 0.0f);
    size_t pos = 0, next = 0;
    while (pos < total)
    {
        while (next < events.size() && static_cast<size_t>(events[next].seconds * rate) <= pos)
        {
            if (events[next].on)
                engine.noteOn(events[next].note, 1.0f);
            else
                engine.noteOff(events[next].note);
            ++next;
        }
        size_t end = std::min(total, pos + static_cast<size_t>(block));
        if (next < events.size())
            end = std::min(end, static_cast<size_t>(events[next].seconds * rate));
        engine.render(out.data() + pos, static_cast<int>(end - pos));
        pos = end;
    }
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(latency));
    return out;
}

double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }
double dB(double x) { return 20.0 * std::log10(std::max(x, 1.0e-9)); }
double hann(size_t i, size_t n) { return 0.5 - 0.5 * std::cos(kPi2 * static_cast<double>(i) / static_cast<double>(n)); }

double amplitude(const std::vector<float>& x, double rate, double from, double length, double f)
{
    const size_t a = static_cast<size_t>(from * rate), n = static_cast<size_t>(length * rate);
    double re = 0.0, im = 0.0, sum = 0.0;
    for (size_t i = 0; i < n && a + i < x.size(); ++i)
    {
        const double w = hann(i, n), v = w * static_cast<double>(x[a + i]), p = kPi2 * f * static_cast<double>(i) / rate;
        re += v * std::cos(p);
        im += v * std::sin(p);
        sum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

double centsOff(const std::vector<float>& x, double rate, double f)
{
    double best = 0.0, at = 0.0;
    for (double c = -30.0; c <= 30.0; c += 1.0)
        if (const double a = amplitude(x, rate, 0.8, 0.5, f * std::pow(2.0, c / 1200.0)); a > best)
        {
            best = a;
            at = c;
        }
    return at;
}

// Frequency from the rising zero crossings inside a window (short windows, glides).
double crossingHz(const std::vector<float>& x, double rate, double from, double to)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = static_cast<size_t>(from * rate) + 1; i < static_cast<size_t>(to * rate) && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (static_cast<double>(i) - static_cast<double>(x[i]) / static_cast<double>(x[i] - x[i - 1])) / rate;
            first = first < 0.0 ? t : first;
            last = t;
            ++count;
        }
    return count > 1 ? (count - 1) / (last - first) : 1.0;
}

double peakIn(const std::vector<float>& x, double rate, double from, double to)
{
    float p = 0.0f;
    for (size_t i = static_cast<size_t>(from * rate); i < static_cast<size_t>(to * rate) && i < x.size(); ++i)
        p = std::max(p, std::abs(x[i]));
    return static_cast<double>(p);
}

// Largest sample step inside a window: a click is a step the held tone never makes.
double stepIn(const std::vector<float>& x, double rate, double from, double to)
{
    float s = 0.0f;
    for (size_t i = static_cast<size_t>(from * rate) + 1; i < static_cast<size_t>(to * rate) && i < x.size(); ++i)
        s = std::max(s, std::abs(x[i] - x[i - 1]));
    return static_cast<double>(s);
}
} // namespace

int runBass808MemphisLab()
{
    struct Spec { const char* name; double low, high, decayMs; };
    constexpr Spec specs[] { { "Core", -25, -15, 360 }, { "Tight", -28, -18, 140 }, { "Dark Long", -35, -22, 720 },
                             { "Bounce", -22, -12, 280 }, { "Hard", -16, -8, 240 }, { "Vintage", -25, -14, 480 } };
    int failures = 0, memphis = 0;
    const auto check = [&failures](bool ok, const juce::String& what)
    {
        if (!ok)
        {
            ++failures;
            std::printf("  FAIL %s\n", what.toRawUTF8());
        }
    };
    for (const auto& p : bassPresets())
        memphis += juce::String(p.name).startsWith("Memphis Trap") ? 1 : 0;
    check(memphis == 6, "the bank holds only the six M808 Memphis presets: " + juce::String(memphis));

    for (const auto& spec : specs)
    {
        const BassPreset* preset = nullptr;
        for (const auto& p : bassPresets())
            if (juce::String(p.name) == "Memphis Trap - M808 " + juce::String(spec.name))
                preset = &p;
        if (preset == nullptr)
        {
            check(false, juce::String("missing ") + spec.name);
            continue;
        }
        const auto& s = preset->settings;
        std::printf("%s\n", preset->name);

        double worstCents = 0.0, worstDc = -200.0, worstPeak = -200.0, worstAlias = -200.0, h25 = 0.0;
        bool finite = true;
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int note : { 24, 29, 33, 36, 48 })
            {
                const auto x = play(s, { { 0.0, note, true } }, 1.6, rate, 512);
                for (const float v : x)
                    finite = finite && std::isfinite(v);
                const double f = hz(note), h1 = amplitude(x, rate, 0.8, 0.5, f);
                worstCents = std::max(worstCents, std::abs(centsOff(x, rate, f)));
                worstPeak = std::max(worstPeak, dB(peakIn(x, rate, 0.0, 1.6)));
                worstDc = std::max(worstDc, dB(0.5 * amplitude(x, rate, 0.8, 0.5, 0.0)));
                if (note == 33)
                {
                    double h = 0.0;
                    for (int k = 2; k <= 5; ++k)
                        h += std::pow(amplitude(x, rate, 0.8, 0.5, f * k), 2.0);
                    h25 = dB(std::sqrt(h) / h1);
                    check(h25 >= spec.low && h25 <= spec.high, "H2-H5 " + juce::String(h25, 1) + " dB at " + juce::String(rate));
                }
                // Aliases land between the harmonics of a held tone; look half way between them.
                for (int k = 2; k < 40 && (k + 0.5) * f < 10000.0; ++k)
                    worstAlias = std::max(worstAlias, dB(amplitude(x, rate, 0.8, 0.5, (k + 0.5) * f) / h1));
            }
        std::printf("  44.1/48/96 kHz, notes 24-48: pitch %.1f c, H2-H5 on A1 %.1f dB, DC %.0f dBFS, peak %.1f dBFS, between harmonics %.0f dB\n",
                    worstCents, h25, worstDc, worstPeak, worstAlias);
        check(finite, "NaN / Inf");
        check(worstCents <= 5.0, "pitch");
        check(worstDc <= -60.0, "DC");
        check(worstPeak <= -1.0, "peak");
        check(worstAlias <= -60.0, "energy between the harmonics");

        constexpr double rate = 48000.0;
        const std::vector<Event> line { { 0.0, 33, true }, { 0.30, 33, false }, { 0.40, 45, true }, { 0.60, 45, false }, { 0.75, 33, true }, { 1.0, 33, false } };
        const auto base = play(s, line, 1.4, rate, 512);
        double diff = 0.0;
        for (const int block : { 32, 64, 128, 1024 })
        {
            const auto other = play(s, line, 1.4, rate, block);
            for (size_t i = 0; i < base.size(); ++i)
                diff = std::max(diff, static_cast<double>(std::abs(base[i] - other[i])));
        }
        std::printf("  blocks 32-1024 differ by %.0f dBFS\n", dB(diff));
        check(dB(diff) < -60.0, "block size changes the sound");

        // gate: 1/32 .. 1/2 at 145 BPM; the tail is gone after the release, the stop makes no click
        const auto held = play(s, { { 0.0, 33, true } }, 1.6, rate, 512);
        const double attackStep = stepIn(held, rate, 0.0, 1.6);
        double worstTail = -200.0, worstStep = 0.0;
        for (const double sixteenths : { 0.5, 1.0, 2.0, 4.0, 8.0 })
        {
            const double off = sixteenths * 60.0 / 145.0 / 4.0;
            const auto x = play(s, { { 0.0, 33, true }, { off, 33, false } }, off + 0.4, rate, 512);
            worstTail = std::max(worstTail, dB(peakIn(x, rate, off + 0.001 * s.releaseMs + 0.010, off + 0.4)));
            worstStep = std::max(worstStep, stepIn(x, rate, off - 0.001, off + 0.001 * s.releaseMs + 0.02) / attackStep);
        }
        // retrigger: the same note, an octave up / down
        double worstRetrigger = 0.0, weakest = 10.0;
        for (const int second : { 33, 45, 21 })
        {
            const auto x = play(s, { { 0.0, 33, true }, { 0.25, 33, false }, { 0.2501, second, true }, { 0.6, second, false } }, 1.0, rate, 512);
            const auto alone = play(s, { { 0.0, second, true } }, 0.6, rate, 512);
            worstRetrigger = std::max(worstRetrigger, stepIn(x, rate, 0.245, 0.30) / std::max(attackStep, stepIn(alone, rate, 0.0, 0.1)));
            weakest = std::min(weakest, peakIn(x, rate, 0.256, 0.33) / peakIn(alone, rate, 0.006, 0.08));
        }
        std::printf("  after release %.0f dBFS; sample step at the stop x%.2f, at a retrigger x%.2f of the held note's largest; retriggered attack x%.2f\n",
                    worstTail, worstStep, worstRetrigger, weakest);
        check(worstTail < -60.0, "release tail");
        check(worstStep <= 1.5, "click at the stop");
        check(worstRetrigger <= 2.0, "click at a retrigger");
        check(weakest >= 0.7, "a retrigger is swallowed");

        // separate notes never slide; overlapping ones only with the glide switched on
        const auto separate = play(s, { { 0.0, 33, true }, { 0.40, 33, false }, { 0.50, 45, true } }, 1.2, rate, 512);
        const auto target = play(s, { { 0.0, 45, true } }, 0.75, rate, 512);
        const double sepEarly = crossingHz(separate, rate, 0.55, 0.63), soloEarly = crossingHz(target, rate, 0.05, 0.13);
        check(std::abs(1200.0 * std::log2(sepEarly / soloEarly)) < 20.0, "separate notes slide");
        const auto overlap = play(s, { { 0.0, 33, true }, { 0.40, 45, true }, { 0.45, 33, false } }, 1.2, rate, 512);
        const double jump = 1200.0 * std::log2(crossingHz(overlap, rate, 0.405, 0.46) / hz(45));
        std::printf("  separate notes %.1f Hz early, alone %.1f Hz; overlap as shipped %.0f c off in its first 55 ms\n", sepEarly, soloEarly, jump);
        check(std::abs(jump) < 60.0, "an overlap slides by default");
        for (const float ms : { 45.0f, 90.0f })
            for (const auto& move : { std::pair<int, int> { 33, 45 }, std::pair<int, int> { 45, 33 } })
            {
                auto g = s;
                g.mode = BassSettings::Glide;
                g.glideMs = ms;
                const auto x = play(g, { { 0.0, move.first, true }, { 0.50, move.second, true }, { 0.55, move.first, false } }, 1.4, rate, 512);
                const double done = 1200.0 * std::log2(crossingHz(x, rate, 0.52 + 0.001 * ms, 0.75 + 0.001 * ms) / hz(move.second));
                check(std::abs(done) < 15.0, "glide " + juce::String(ms) + " ms ends " + juce::String(done, 1) + " c off");
            }

        // envelope: when the level is within 0.5 dB of the sustain (windows of four periods)
        const auto longNote = play(s, { { 0.0, 33, true } }, 5.0, rate, 512);
        const double steady = amplitude(longNote, rate, 4.2, 0.6, 55.0);
        double reached = 0.0;
        for (double t = 0.0; t < 3.0; t += 0.01)
            if (std::abs(dB(amplitude(longNote, rate, t, 4.0 / 55.0, 55.0) / steady)) > 0.5)
                reached = t + 2.0 / 55.0;
        check(std::abs(1000.0 * reached / spec.decayMs - 1.0) < 0.15, "decay time");
        std::printf("  decay to within 0.5 dB of the sustain: %.0f ms (specified %.0f)\n", 1000.0 * reached, spec.decayMs);

        const auto start = std::chrono::steady_clock::now();
        (void) play(s, { { 0.0, 33, true } }, 30.0, rate, 512);
        std::printf("  CPU %.2f ms per second of audio\n",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 30.0);
    }
    std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
