#include "BassWavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace bbg::wt
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kCycle = 2048; // the generating resolution (bins up to 1024; 512 are kept)
using Complex = std::complex<double>;

constexpr const char* kNames[] { "Basic Shapes", "Kick Pulse", "Harmonic Sweep", "PWM", "Hard Sync", "Fold", "Vowel", "Growl" };
constexpr int kTables = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// in-place radix-2 FFT (n a power of two); inverse: conjugate trick, scaled by 1 / n
void fft(std::vector<Complex>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const Complex w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            Complex u(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const Complex x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
    if (inverse)
        for (auto& x : a)
            x /= static_cast<double>(n);
}

double saw(double x) { return 2.0 * (x - std::floor(x)) - 1.0; }

// one cycle (x in 0 .. 1) of table t at position p (0 .. 1), before band-limiting
double generate(int t, double p, double x)
{
    switch (t)
    {
        case 0: // sine -> triangle -> saw -> square, crossfaded
        {
            const double sine = std::sin(2.0 * kPi * x);
            const double tri = x < 0.25 ? 4.0 * x : x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0; // starts at 0, rising (as the sine)
            const double sw = saw(x + 0.5);                                                  // 0 at x = 0, rising
            const double sq = x < 0.5 ? 1.0 : -1.0;
            const double s = 3.0 * p;
            if (s < 1.0) return (1.0 - s) * sine + s * tri;
            if (s < 2.0) return (2.0 - s) * tri + (s - 1.0) * sw;
            return (3.0 - s) * sw + (s - 2.0) * sq;
        }
        case 1: // the cycle runs fast and slows (exponential phase warp), then a soft drive
        {
            const double c = 0.01 + 7.0 * p, drive = 1.0 + 5.0 * p;
            const double w = (1.0 - std::exp(-c * x)) / (1.0 - std::exp(-c));
            return std::tanh(drive * std::sin(2.0 * kPi * w)) / std::tanh(drive);
        }
        case 2: // saw harmonics 1 .. N, N = 1 .. 64 (the last one fading in)
        {
            const double n = 1.0 + 63.0 * p;
            double y = 0.0;
            for (int k = 1; k <= 64; ++k)
            {
                const double a = std::clamp(n - k + 1.0, 0.0, 1.0);
                if (a <= 0.0)
                    break;
                y += a * std::sin(2.0 * kPi * k * x) / k;
            }
            return y;
        }
        case 3: // pulse width 50 % -> 5 %
            return x < 0.5 - 0.45 * p ? 1.0 : -1.0;
        case 4: // a saw restarted at each cycle of the note, running ratio times as fast
            return saw((1.0 + 7.0 * p) * x + 0.5);
        case 5: // folded sine
        {
            const double g = 1.0 + 7.0 * p;
            return std::sin(0.5 * kPi * g * std::sin(2.0 * kPi * x));
        }
        case 6: // formants over a C2 series
        {
            static constexpr double f[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            const double s = 4.0 * p;
            const int a = std::min(3, static_cast<int>(s));
            const double u = s - a;
            double y = 0.0;
            for (int k = 1; k <= 64; ++k)
            {
                const double hz = 65.406 * k;
                double amp = 0.15 / k; // a little of the plain series under the formants
                for (int i = 0; i < 3; ++i)
                {
                    const double centre = (1.0 - u) * f[a][i] + u * f[a + 1][i];
                    const double width = 0.18 * centre;
                    amp += (i == 0 ? 1.0 : i == 1 ? 0.6 : 0.3) * std::exp(-0.5 * std::pow((hz - centre) / width, 2.0)) / std::sqrt(k);
                }
                y += amp * std::sin(2.0 * kPi * k * x);
            }
            return y;
        }
        default: // growl: phase modulation by the 3rd harmonic
            return std::sin(2.0 * kPi * x + 3.0 * p * std::sin(2.0 * kPi * 3.0 * x));
    }
}
// The tables made of sine series (Harmonic Sweep, Vowel) straight into the spectrum, as the time-domain generation
// would give it (the half-sample grid included): the same frames, without 2048 x 64 sines per frame at start-up.
bool additive(int t, double p, std::vector<Complex>& spectrum)
{
    if (t != 2 && t != 6)
        return false;
    std::fill(spectrum.begin(), spectrum.end(), Complex());
    static constexpr double f[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    const double s = 4.0 * p;
    const int v = std::min(3, static_cast<int>(s));
    const double u = s - v;
    for (int k = 1; k <= 64; ++k)
    {
        double amp = 0.0;
        if (t == 2)
            amp = std::clamp(1.0 + 63.0 * p - k + 1.0, 0.0, 1.0) / k;
        else
        {
            const double hz = 65.406 * k;
            amp = 0.15 / k;
            for (int i = 0; i < 3; ++i)
            {
                const double centre = (1.0 - u) * f[v][i] + u * f[v + 1][i];
                amp += (i == 0 ? 1.0 : i == 1 ? 0.6 : 0.3) * std::exp(-0.5 * std::pow((hz - centre) / (0.18 * centre), 2.0)) / std::sqrt(k);
            }
        }
        // a * sin(2 pi k x) sampled at (n + 0.5) / N: X_k = N a / (2 i) e^(i pi k / N), X_(N-k) its conjugate
        const Complex bin = static_cast<double>(kCycle) * amp / Complex(0.0, 2.0) * std::polar(1.0, kPi * k / kCycle);
        spectrum[static_cast<size_t>(k)] = bin;
        spectrum[static_cast<size_t>(kCycle - k)] = std::conj(bin);
    }
    return true;
}
} // namespace

const Bank& Bank::get()
{
    static const Bank bank;
    return bank;
}

int Bank::count() noexcept { return kTables; }

const char* Bank::name(int table) noexcept { return kNames[std::clamp(table, 0, kTables - 1)]; }

int Bank::levelFor(double dt) noexcept
{
    // the richest level whose top harmonic stays under 0.45 x the sample rate
    const double allowed = 0.45 / std::max(1.0e-9, dt);
    int level = 0;
    while (level < kLevels - 1 && static_cast<double>(kTopHarmonics >> level) > allowed)
        ++level;
    return level;
}

Bank::Bank()
{
    tables.resize(kTables);
    std::vector<Complex> spectrum(kCycle);
    for (int t = 0; t < kTables; ++t)
    {
        for (int l = 0; l < kLevels; ++l)
        {
            auto& level = tables[static_cast<size_t>(t)][static_cast<size_t>(l)];
            level.size = std::max(256, 8 * (kTopHarmonics >> l)); // 8 samples per period of its top harmonic
            level.frames.assign(static_cast<size_t>(kFrames * (level.size + 3)), 0.0f);
        }
        for (int f = 0; f < kFrames; ++f)
        {
            const double p = static_cast<double>(f) / (kFrames - 1);
            if (!additive(t, p, spectrum))
            {
                for (int n = 0; n < kCycle; ++n)
                    spectrum[static_cast<size_t>(n)] = Complex(generate(t, p, (n + 0.5) / kCycle), 0.0);
                fft(spectrum, false);
            }
            double scale = 0.0;
            for (int l = 0; l < kLevels; ++l)
            {
                auto& level = tables[static_cast<size_t>(t)][static_cast<size_t>(l)];
                const int keep = kTopHarmonics >> l;
                std::vector<Complex> cycle(static_cast<size_t>(level.size), Complex());
                for (int k = 1; k <= keep && k < level.size / 2; ++k)
                {
                    // the half-sample offset of the generating grid undone, so every level starts at the same phase
                    const Complex shift = std::polar(1.0, -kPi * k / kCycle);
                    const Complex bin = spectrum[static_cast<size_t>(k)] * shift / static_cast<double>(kCycle);
                    cycle[static_cast<size_t>(k)] = bin * static_cast<double>(level.size);
                    cycle[static_cast<size_t>(level.size - k)] = std::conj(bin) * static_cast<double>(level.size);
                }
                fft(cycle, true);
                float* out = level.frames.data() + static_cast<size_t>(f * (level.size + 3)) + 1; // out[-1] .. out[size + 1]
                if (l == 0)
                {
                    // every frame to peak 1 at the richest level; the others share its scale (same loudness at any pitch)
                    double peak = 0.0;
                    for (const auto& x : cycle)
                        peak = std::max(peak, std::abs(x.real()));
                    scale = peak > 1.0e-9 ? 1.0 / peak : 0.0;
                }
                for (int n = 0; n < level.size; ++n)
                    out[n] = static_cast<float>(cycle[static_cast<size_t>(n)].real() * scale);
                out[-1] = out[level.size - 1]; // the guard samples: interpolation across the wrap
                out[level.size] = out[0];
                out[level.size + 1] = out[1];
            }
        }
    }
}

double Bank::read(int table, double pos, double phase, double dt) const noexcept
{
    const auto& level = tables[static_cast<size_t>(std::clamp(table, 0, kTables - 1))][static_cast<size_t>(levelFor(dt))];
    const double fp = std::clamp(pos, 0.0, 1.0) * (kFrames - 1);
    const int f0 = std::min(kFrames - 2, static_cast<int>(fp));
    const double ff = fp - f0;
    const double x = (phase - std::floor(phase)) * level.size;
    const int i0 = std::min(level.size - 1, static_cast<int>(x));
    const double t = x - i0;
    const float* a = level.frames.data() + static_cast<size_t>(f0 * (level.size + 3)) + 1 + i0;
    const float* b = a + (level.size + 3);
    // 4-point, 3rd-order Hermite (Catmull-Rom)
    const auto hermite = [t](const float* p)
    {
        const double xm = p[-1], x0 = p[0], x1 = p[1], x2 = p[2];
        const double c1 = 0.5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    };
    const double va = hermite(a), vb = hermite(b);
    return va + ff * (vb - va);
}
} // namespace bbg::wt
