// Wavetable oscillator and sub harmonics (Bass Architecture 2.0): `--wavetable-check`.
//   1 aliasing: every table at Position 0 / 0.5 / 1, MIDI 36 .. 96: the energy off the note's harmonics < -50 dB
//   2 level: every frame of every table peaks at 1 (+-0.05) at the richest level: a Position sweep keeps its level
//   3 morph: an LFO sweeping Position (4 Hz) makes no step: its largest sample-to-sample move stays within the static
//     frames' own (x 1.15)
//   4 sub harmonics: H3 0.49 / H5 0.06 give exactly those partials (+-0.01) and nothing else (< -60 dB)
//   5 CPU: three wavetable oscillators x 7 unison voices, real-time factor at 48 kHz
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
#include "Bass808/BassEngine.h"
#include "Bass808/BassWavetable.h"

namespace
{
using namespace bbg;
constexpr double kRate = 48000.0;

BassSettings wavetableSound(int table, float pos, int unison = 1)
{
    BassSettings s;
    s.engine = BassSettings::Advanced;
    auto& o = s.advOsc[0];
    o.on = true;
    o.wave = BassSettings::AdvWavetable;
    o.table = table;
    o.wtPos = pos;
    o.levelDb = -6.0f;
    o.unison = unison;
    o.randomPhase = false;
    s.advAmpEnv = { 1.0f, 300.0f, 1.0f, 50.0f };
    s.advFilterAType = BassSettings::LP24;
    s.advCutoffA = 20000.0f;
    s.mode = BassSettings::Retrigger;
    return s;
}

// magnitude spectrum of x[from .. from + n) (Hann), n a power of two
std::vector<double> spectrum(const std::vector<float>& x, size_t from, size_t n)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i)
    {
        // Blackman-Harris (4 term, sidelobes -92 dB): the window's leakage stays under what is measured
        const double u = 2.0 * 3.14159265358979 * i / (n - 1);
        a[i] = x[from + i] * (0.35875 - 0.48829 * std::cos(u) + 0.14128 * std::cos(2.0 * u) - 0.01168 * std::cos(3.0 * u));
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w = std::polar(1.0, -2.0 * 3.14159265358979 / len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u = 1.0;
            for (size_t k = 0; k < len / 2; ++k, u *= w)
            {
                const auto p = a[i + k], q = a[i + k + len / 2] * u;
                a[i + k] = p + q;
                a[i + k + len / 2] = p - q;
            }
        }
    }
    std::vector<double> mag(n / 2);
    for (size_t k = 0; k < n / 2; ++k)
        mag[k] = std::abs(a[k]);
    return mag;
}

// energy off the harmonics of f0 (outside +-4 bins of each), dB re all
double offHarmonicDb(const std::vector<double>& mag, double f0, size_t n)
{
    const double binHz = kRate / static_cast<double>(n);
    double on = 0.0, off = 0.0;
    for (size_t k = 1; k < mag.size(); ++k)
    {
        const double hz = k * binHz;
        const double h = std::round(hz / f0);
        const bool near = h >= 1.0 && std::abs(hz - h * f0) <= 4.0 * binHz;
        (near ? on : off) += mag[k] * mag[k];
    }
    return 10.0 * std::log10((off + 1.0e-30) / (on + off + 1.0e-30));
}
} // namespace

int checkWavetables()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
        failures += ok ? 0 : 1;
    };
    const auto t0 = std::chrono::steady_clock::now();
    const auto& bank = wt::Bank::get();
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("tables built in %.0f ms\n", buildMs);

    // 1 aliasing
    double worstAlias = -300.0;
    for (int table = 0; table < wt::Bank::count(); ++table)
        for (const float pos : { 0.0f, 0.5f, 1.0f })
            for (const int note : { 36, 48, 60, 72, 84, 96 })
            {
                const auto audio = BassEngine::renderNote(wavetableSound(table, pos), nullptr, note, 0.6, kRate);
                const auto mag = spectrum(audio, 8192, 16384);
                const double f0 = BassEngine::noteHz(note, 0.0f);
                const double db = offHarmonicDb(mag, f0, 16384);
                if (db > worstAlias)
                {
                    worstAlias = db;
                    std::printf("   worst so far: %s pos %.1f MIDI %d: %.1f dB off the harmonics\n", wt::Bank::name(table), pos, note, db);
                }
            }
    check(worstAlias < -50.0, "1 every table, Position and note: off-harmonic energy under -50 dB (no aliasing)");

    // 2 frame levels at the richest level (dt tiny)
    double worstLevel = 0.0;
    for (int table = 0; table < wt::Bank::count(); ++table)
        for (int f = 0; f < wt::Bank::kFrames; ++f)
        {
            const double pos = static_cast<double>(f) / (wt::Bank::kFrames - 1);
            double peak = 0.0;
            for (int i = 0; i < 4096; ++i)
                peak = std::max(peak, std::abs(bank.read(table, pos, i / 4096.0, 1.0e-5)));
            worstLevel = std::max(worstLevel, std::abs(peak - 1.0));
        }
    std::printf("   frame peaks within %.3f of 1\n", worstLevel);
    check(worstLevel < 0.05, "2 every frame peaks at 1: a Position sweep keeps its level");

    // 3 morph without steps (sine-like tables)
    for (const int table : { 1, 2, 5 })
    {
        const auto maxStep = [](const std::vector<float>& a)
        {
            double m = 0.0;
            for (size_t i = 4800; i + 1 < a.size(); ++i)
                m = std::max(m, std::abs(static_cast<double>(a[i + 1]) - a[i]));
            return m;
        };
        double still = 0.0;
        for (const float pos : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            still = std::max(still, maxStep(BassEngine::renderNote(wavetableSound(table, pos), nullptr, 36, 0.5, kRate)));
        auto moving = wavetableSound(table, 0.5f);
        moving.advLfo[0] = { 0, 4.0f, 0, true };
        moving.advMod[0] = { BassSettings::ModLfo1, BassSettings::DestOscAWt, 0.5f };
        const double swept = maxStep(BassEngine::renderNote(moving, nullptr, 36, 1.0, kRate));
        std::printf("   %s: largest step still %.4f, swept %.4f\n", wt::Bank::name(table), still, swept);
        check(swept <= 1.15 * still, "3 a Position sweep makes no step");
    }

    // 4 the sub's harmonics
    {
        BassSettings s;
        s.engine = BassSettings::Advanced;
        s.advSubOn = true;
        s.advSubOctave = 0;
        s.advSubLevelDb = -6.0f;
        s.advSubH3 = 0.49f;
        s.advSubH5 = 0.06f;
        s.advAmpEnv = { 1.0f, 300.0f, 1.0f, 50.0f };
        s.mode = BassSettings::Retrigger;
        const auto audio = BassEngine::renderNote(s, nullptr, 31, 1.0, kRate);
        const size_t n = 32768;
        const auto mag = spectrum(audio, 9000, n);
        const double f0 = BassEngine::noteHz(31, 0.0f), binHz = kRate / n;
        const auto partial = [&](int h)
        {
            double e = 0.0;
            for (size_t k = 0; k < mag.size(); ++k)
                if (std::abs(k * binHz - h * f0) <= 6.0 * binHz)
                    e += mag[k] * mag[k];
            return std::sqrt(e);
        };
        const double p1 = partial(1), p3 = partial(3) / p1, p5 = partial(5) / p1;
        double others = 0.0, all = 0.0;
        for (size_t k = 1; k < mag.size(); ++k)
        {
            all += mag[k] * mag[k];
            const double h = std::round(k * binHz / f0);
            const bool listed = (h == 1.0 || h == 3.0 || h == 5.0) && std::abs(k * binHz - h * f0) <= 6.0 * binHz;
            others += listed ? 0.0 : mag[k] * mag[k];
        }
        const double otherDb = 10.0 * std::log10((others + 1.0e-30) / all);
        std::printf("   sub: H3 %.3f H5 %.3f, everything else %.1f dB\n", p3, p5, otherDb);
        check(std::abs(p3 - 0.49) < 0.01 && std::abs(p5 - 0.06) < 0.01 && otherDb < -60.0, "4 the sub's 3rd / 5th harmonics exact");
    }

    // 5 CPU: three wavetable oscillators, 7 voices each
    {
        auto s = wavetableSound(1, 0.4f, 7);
        for (int i = 1; i < 3; ++i)
        {
            s.advOsc[static_cast<size_t>(i)] = s.advOsc[0];
            s.advOsc[static_cast<size_t>(i)].table = i == 1 ? 0 : 7;
        }
        // the best of three: a timing check, not a load test (another process on the machine slows one run)
        double seconds = 1.0e9;
        std::vector<float> audio;
        for (int run = 0; run < 3; ++run)
        {
            const auto t = std::chrono::steady_clock::now();
            audio = BassEngine::renderNote(s, nullptr, 31, 10.0, kRate);
            seconds = std::min(seconds, std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count());
        }
        std::printf("   3 wavetable oscillators x 7 voices: %.1f x real time (%.2f %% of a core)\n", 10.0 / seconds, 100.0 * seconds / 10.0);
        check(10.0 / seconds > 20.0 && !audio.empty(), "5 21 wavetable voices render over 20 x real time");
    }
    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
