#include "BassAdvanced.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace bbg::adv
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
double wrap(double t) noexcept { return t - std::floor(t); }

double polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double blepSaw(double t, double dt) noexcept { return 2.0 * t - 1.0 - polyBlep(t, dt); }

// Zavalishin / Cytomic trapezoidal SVF, one sample; ic: the stage's two integrator states
inline void svfTick(double v0, double g, double k, double& ic1, double& ic2, double& low, double& band, double& high) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = v0 - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    low = v2;
    band = v1;
    high = v0 - k * v1 - v2;
}

// one filter (LP12, LP24, HP12, BP) on one channel; st: [ic1, ic2] x 2 stages
double filterSample(double x, int type, double g, double k, std::array<double, 4>& st) noexcept
{
    double low = 0.0, band = 0.0, high = 0.0;
    svfTick(x, g, k, st[0], st[1], low, band, high);
    switch (type)
    {
        case BassSettings::LP24:
        {
            double low2 = 0.0, band2 = 0.0, high2 = 0.0;
            svfTick(low, g, k, st[2], st[3], low2, band2, high2);
            return low2;
        }
        case BassSettings::HP12: return high;
        case BassSettings::BP: return band;
        default: return low;
    }
}

double cutoffG(double hz, double sampleRate) noexcept
{
    return std::tan(kPi * juce::jlimit(16.0, 0.45 * sampleRate, hz) / sampleRate);
}

// resonance 0 .. 1 -> damping k 2 .. 0.06 (self-oscillation stays just out of reach)
double damping(double resonance) noexcept { return 2.0 - 1.94 * juce::jlimit(0.0, 1.0, resonance); }

const std::array<double, 9> kSyncBeats { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
} // namespace

// ------------------------------------------------------------------------------------------ envelope
void Env::set(const BassSettings::AdvEnv& e, double sampleRate) noexcept
{
    attackStep = 1.0 / juce::jmax(1.0, e.attackMs * 0.001 * sampleRate);
    // decay / release: exponential to 1 / 1000 of the way in their time
    decayCoef = std::exp(std::log(0.001) / juce::jmax(1.0, e.decayMs * 0.001 * sampleRate));
    releaseCoef = std::exp(std::log(0.001) / juce::jmax(1.0, e.releaseMs * 0.001 * sampleRate));
    sustain = juce::jlimit(0.0, 1.0, static_cast<double>(e.sustain));
}

double Env::tick() noexcept
{
    switch (stage)
    {
        case Attack:
            level += attackStep;
            if (level >= 1.0)
            {
                level = 1.0;
                stage = Decay;
            }
            break;
        case Decay:
            level = sustain + (level - sustain) * decayCoef;
            if (std::abs(level - sustain) < 1.0e-5)
            {
                level = sustain;
                stage = sustain > 0.0 ? Sustain : Off;
            }
            break;
        case Sustain: level = sustain; break;
        case Release:
            level *= releaseCoef;
            if (level < 1.0e-5)
            {
                level = 0.0;
                stage = Off;
            }
            break;
        case Off: level = 0.0; break;
    }
    return level;
}

// ----------------------------------------------------------------------------------------------- LFO
double Lfo::random() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<double>(seed >> 8) / 8388608.0 - 1.0;
}

double Lfo::tick(int shape, double dt) noexcept
{
    const double before = phase;
    phase = wrap(phase + dt);
    const bool wrapped = phase < before;
    switch (shape)
    {
        case 1: return 1.0 - 4.0 * std::abs(before - 0.5);
        case 2: return 2.0 * before - 1.0;
        case 3: return 1.0 - 2.0 * before;
        case 4: return before < 0.5 ? 1.0 : -1.0;
        case 5:
            if (wrapped)
                held = random();
            return held;
        case 6:
            if (wrapped)
            {
                from = to;
                to = random();
            }
            return from + (to - from) * (0.5 - 0.5 * std::cos(kPi * before));
        default: return std::sin(kTwoPi * before);
    }
}

// --------------------------------------------------------------------------------------------- voice
void Voice::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    tables = &wt::Bank::get(); // the first call builds the tables (allocates): here, not while rendering
}

void Voice::setSettings(const BassSettings& settings) noexcept
{
    s = settings;
    // the envelopes' times follow the knobs at once; their stage and level stay
    amp.set(s.advAmpEnv, sampleRate);
    filterEnv.set(s.advFilterEnv, sampleRate);
    modEnv.set(s.advModEnv, sampleRate);
}

double Voice::lfoHz(const BassSettings::AdvLfo& l) const noexcept
{
    const int sync = juce::jlimit(0, 8, l.sync);
    return sync == 0 ? static_cast<double>(l.rateHz) : tempo / 60.0 / kSyncBeats[static_cast<size_t>(sync)];
}

void Voice::start(float newVelocity, int newNote, double newTempo) noexcept
{
    velocity = newVelocity;
    note = newNote;
    tempo = newTempo > 20.0 ? newTempo : 120.0;
    std::uint32_t seed = noiseSeed ^ static_cast<std::uint32_t>(newNote * 2654435761u);
    const auto random01 = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double>(seed >> 8) / 16777216.0;
    };
    for (size_t i = 0; i < osc.size(); ++i)
    {
        const auto& o = s.advOsc[i];
        for (int v = 0; v < kMaxUnison; ++v)
            osc[i].phase[static_cast<size_t>(v)] = (v == 0 || !o.randomPhase) ? wrap(o.phase) : random01();
    }
    subPhase = 0.0; // a zero crossing of the sine: the sub starts without a click
    amp.level = 0.0;
    amp.start();
    filterEnv.level = modEnv.level = 0.0;
    filterEnv.start();
    modEnv.start();
    for (size_t i = 0; i < lfo.size(); ++i)
        if (s.advLfo[i].retrigger)
            lfo[i].phase = 0.0;
    for (auto& channel : svfA)
        channel.fill(0.0);
    for (auto& channel : svfB)
        channel.fill(0.0);
    ctl.started = false;
    controlLeft = 0;
}

void Voice::release() noexcept
{
    // the amp's release is the voice's fade (BassVoice); these follow it
    filterEnv.release();
    modEnv.release();
}

double Voice::oscSample(int wave, double t, double dt, double shape) const noexcept
{
    switch (wave)
    {
        case BassSettings::AdvTriangle:
        {
            const double tri = 1.0 - 4.0 * std::abs(t - 0.5);
            if (shape <= 0.001)
                return tri;
            return std::sin(0.5 * kPi * tri * (1.0 + 4.0 * shape));
        }
        case BassSettings::AdvSaw:
        {
            const double a = blepSaw(t, dt);
            if (shape <= 0.001)
                return a;
            const double up = blepSaw(wrap(2.0 * t), juce::jmin(0.5, 2.0 * dt));
            return (1.0 - 0.5 * shape) * a + 0.5 * shape * up;
        }
        case BassSettings::AdvSquare:
        case BassSettings::AdvPulse:
        {
            const double width = wave == BassSettings::AdvSquare ? 0.5 : 0.5 - 0.45 * juce::jlimit(0.0, 1.0, shape);
            // a pulse = a saw minus a saw shifted by the width (both band-limited), centred
            const double p = blepSaw(t, dt) - blepSaw(wrap(t + 1.0 - width), dt);
            return p - (2.0 * width - 1.0);
        }
        default:
        {
            const double sine = std::sin(kTwoPi * t);
            if (shape <= 0.001)
                return sine;
            return std::sin(0.5 * kPi * sine * (1.0 + 4.0 * shape));
        }
    }
}

void Voice::updateControl(double hz) noexcept
{
    // every kControl samples: the modulation (the LFOs step a block, the envelopes as they are now), then each
    // oscillator's per-voice increments and pans, the levels and the filters' coefficients; levels and
    // coefficients glide over the block, so nothing steps
    std::array<double, BassSettings::NumModSources> src {};
    for (size_t i = 0; i < lfo.size(); ++i)
        src[BassSettings::ModLfo1 + i] = lfo[i].tick(s.advLfo[i].shape, kControl * lfoHz(s.advLfo[i]) / sampleRate);
    src[BassSettings::ModFilterEnv] = filterEnv.level;
    src[BassSettings::ModModEnv] = modEnv.level;
    src[BassSettings::ModAmpEnv] = amp.level;
    src[BassSettings::ModVelocity] = velocity;
    src[BassSettings::ModNote] = (note - 48) / 24.0;
    std::array<double, BassSettings::NumModDests> mod {};
    for (const auto& m : s.advMod)
        if (m.source > 0 && m.source < BassSettings::NumModSources && m.dest > 0 && m.dest < BassSettings::NumModDests)
            mod[static_cast<size_t>(m.dest)] += m.amount * src[static_cast<size_t>(m.source)];

    const double pitchAll = mod[BassSettings::DestPitch] * 24.0;
    for (int i = 0; i < 3; ++i)
    {
        const auto& o = s.advOsc[static_cast<size_t>(i)];
        auto& c = ctl.osc[static_cast<size_t>(i)];
        c.on = o.on;
        if (!o.on)
            continue;
        const double semis = o.octave * 12.0 + o.semitones + o.fine * 0.01 + pitchAll
                           + mod[static_cast<size_t>(BassSettings::DestOscAPitch + i)] * 24.0;
        const double f = hz * std::exp2(semis / 12.0) / sampleRate;
        c.voices = juce::jlimit(1, kMaxUnison, o.unison);
        const double detune = juce::jlimit(0.0, 1.0, o.detune + mod[BassSettings::DestDetune]) * 50.0;
        const double norm = 1.0 / std::sqrt(static_cast<double>(c.voices));
        for (int v = 0; v < c.voices; ++v)
        {
            const double position = c.voices == 1 ? 0.0 : 2.0 * v / (c.voices - 1) - 1.0;
            c.dt[static_cast<size_t>(v)] = juce::jmin(0.5, f * std::exp2(position * detune / 1200.0));
            const double pan = juce::jlimit(-1.0, 1.0, o.pan + position * o.spread);
            c.panL[static_cast<size_t>(v)] = norm * juce::jmin(1.0, 1.0 - pan);
            c.panR[static_cast<size_t>(v)] = norm * juce::jmin(1.0, 1.0 + pan);
        }
        c.norm = norm;
        c.shape = juce::jlimit(0.0, 1.0, o.shape + mod[static_cast<size_t>(BassSettings::DestOscAShape + i)]);
        c.pm = (o.pmFrom > i && o.pmFrom < 3)
            ? juce::jlimit(0.0, 1.0, o.pmAmount + mod[static_cast<size_t>(BassSettings::DestOscAPm + i)]) * 4.0 / kTwoPi : 0.0;
        c.level.target = o.audible ? dbToGain(o.levelDb + mod[static_cast<size_t>(BassSettings::DestOscALevel + i)] * 24.0) : 0.0;
        c.table = juce::jlimit(0, wt::Bank::count() - 1, o.table);
        c.wtPos.target = juce::jlimit(0.0, 1.0, static_cast<double>(o.wtPos) + mod[static_cast<size_t>(BassSettings::DestOscAWt + i)]);
    }
    ctl.noise.target = (s.advNoiseDb > -59.5f || mod[BassSettings::DestNoise] > 0.0)
        ? dbToGain(s.advNoiseDb + mod[BassSettings::DestNoise] * 24.0) : 0.0;
    const double driveDb = juce::jlimit(0.0, 36.0, s.advDriveDb + mod[BassSettings::DestDrive] * 24.0);
    ctl.driveOn = driveDb > 0.01;
    ctl.driveG = dbToGain(driveDb);
    ctl.driveNorm = 1.0 / std::tanh(ctl.driveG);
    const double keyOct = s.advKeyA * std::log2(juce::jmax(1.0, hz) / 65.406); // C2 = no change
    const double cutA = std::exp2(std::log2(juce::jmax(16.0f, s.advCutoffA)) + keyOct + s.advEnvA * filterEnv.level
                                  + mod[BassSettings::DestCutoffA] * 6.0);
    const double cutB = std::exp2(std::log2(juce::jmax(16.0f, s.advCutoffB)) + mod[BassSettings::DestCutoffB] * 6.0);
    const bool lowPass = s.advFilterAType == BassSettings::LP12 || s.advFilterAType == BassSettings::LP24;
    ctl.bypassA = lowPass && cutA >= 19500.0 && s.advResoA + mod[BassSettings::DestResoA] <= 0.001;
    ctl.gA.target = cutoffG(cutA, sampleRate);
    ctl.kA.target = damping(s.advResoA + mod[BassSettings::DestResoA]);
    ctl.gB.target = cutoffG(cutB, sampleRate);
    ctl.kB.target = damping(s.advResoB);
    ctl.width.target = juce::jlimit(0.0, 2.0, s.advWidth + mod[BassSettings::DestWidth]);
    ctl.mid.target = dbToGain(s.advMidDb) * juce::jlimit(0.0, 2.0, 1.0 + mod[BassSettings::DestAmp]);
    ctl.subDt = hz * std::exp2(juce::jlimit(-2, 0, s.advSubOctave)) / sampleRate;
    ctl.sub.target = s.advSubOn ? dbToGain(s.advSubLevelDb + mod[BassSettings::DestSub] * 24.0) : 0.0;
    ctl.subWarm = 1.0 + 3.0 * juce::jlimit(0.0f, 1.0f, s.advSubWarm);
    ctl.subWarmNorm = 1.0 / std::tanh(ctl.subWarm);

    auto ramps = ctl.ramps();
    if (!ctl.started)
    {
        for (auto* r : ramps)
            r->now = r->target;
        ctl.started = true;
    }
    for (auto* r : ramps)
        r->step = (r->target - r->now) / kControl;
}

void Voice::tick(double hz, double gain, float& subOut, float& left, float& right) noexcept
{
    if (controlLeft <= 0)
    {
        updateControl(hz);
        controlLeft = kControl;
    }
    --controlLeft;
    for (auto* r : ctl.ramps())
        r->now += r->step;
    filterEnv.tick();
    modEnv.tick();
    const double ampLevel = amp.tick();

    // oscillators: C first, then B, then A, so PM always takes a higher-index oscillator's fresh output
    double midL = 0.0, midR = 0.0;
    std::array<double, 3> outNow {};
    for (int i = 2; i >= 0; --i)
    {
        const auto& c = ctl.osc[static_cast<size_t>(i)];
        if (!c.on)
            continue;
        const auto& o = s.advOsc[static_cast<size_t>(i)];
        auto& state = osc[static_cast<size_t>(i)];
        const double pm = c.pm != 0.0 ? c.pm * outNow[static_cast<size_t>(o.pmFrom)] : 0.0;
        double mono = 0.0, l = 0.0, r = 0.0;
        for (int v = 0; v < c.voices; ++v)
        {
            auto& ph = state.phase[static_cast<size_t>(v)];
            const double dt = c.dt[static_cast<size_t>(v)];
            const double t = pm != 0.0 ? wrap(ph + pm) : ph;
            const double x = o.wave == BassSettings::AdvWavetable && tables != nullptr ? tables->read(c.table, c.wtPos.now, t, dt)
                                                                                       : oscSample(o.wave, t, dt, c.shape);
            ph += dt;
            if (ph >= 1.0)
                ph -= 1.0;
            mono += x;
            l += x * c.panL[static_cast<size_t>(v)];
            r += x * c.panR[static_cast<size_t>(v)];
        }
        outNow[static_cast<size_t>(i)] = mono * c.norm;
        midL += l * c.level.now;
        midR += r * c.level.now;
    }
    if (ctl.noise.now > 1.0e-6)
    {
        const auto white = [this]
        {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            return static_cast<double>(noiseSeed >> 8) / 8388608.0 - 1.0;
        };
        midL += ctl.noise.now * white();
        midR += ctl.noise.now * white();
    }
    if (ctl.driveOn)
    {
        midL = std::tanh(ctl.driveG * midL) * ctl.driveNorm;
        midR = std::tanh(ctl.driveG * midR) * ctl.driveNorm;
    }
    double aL = ctl.bypassA ? midL : filterSample(midL, s.advFilterAType, ctl.gA.now, ctl.kA.now, svfA[0]);
    double aR = ctl.bypassA ? midR : filterSample(midR, s.advFilterAType, ctl.gA.now, ctl.kA.now, svfA[1]);
    if (s.advFilterBOn)
    {
        if (s.advFiltersParallel)
        {
            aL = 0.5 * (aL + filterSample(midL, s.advFilterBType, ctl.gB.now, ctl.kB.now, svfB[0]));
            aR = 0.5 * (aR + filterSample(midR, s.advFilterBType, ctl.gB.now, ctl.kB.now, svfB[1]));
        }
        else
        {
            aL = filterSample(aL, s.advFilterBType, ctl.gB.now, ctl.kB.now, svfB[0]);
            aR = filterSample(aR, s.advFilterBType, ctl.gB.now, ctl.kB.now, svfB[1]);
        }
    }
    const double m = 0.5 * (aL + aR), sd = 0.5 * (aL - aR) * ctl.width.now;
    const double env = ampLevel * gain * ctl.mid.now;
    left += static_cast<float>((m + sd) * env);
    right += static_cast<float>((m - sd) * env);

    // the sub: its own oscillator, the note's pitch and glide, the amp envelope, nothing else
    if (s.advSubOn)
    {
        double x = s.advSubWave == 1 ? 1.0 - 4.0 * std::abs(subPhase - 0.5) : std::sin(kTwoPi * subPhase);
        if (s.advSubH3 > 0.0005f)
            x += s.advSubH3 * std::sin(3.0 * kTwoPi * subPhase);
        if (s.advSubH5 > 0.0005f)
            x += s.advSubH5 * std::sin(5.0 * kTwoPi * subPhase);
        subPhase += ctl.subDt;
        if (subPhase >= 1.0)
            subPhase -= 1.0;
        if (ctl.subWarm > 1.003)
            x = std::tanh(ctl.subWarm * x) * ctl.subWarmNorm;
        subOut += static_cast<float>(x * ctl.sub.now * ampLevel * gain);
    }
}

// ------------------------------------------------------------------------------------------- mid FX
void MidFx::prepare(double newSampleRate, int)
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    satL.prepare(sampleRate);
    satR.prepare(sampleRate);
    chorusL.assign(static_cast<size_t>(0.06 * sampleRate) + 4, 0.0f);
    chorusR = chorusL;
    reset();
}

void MidFx::reset() noexcept
{
    satL.reset();
    satR.reset();
    for (auto& channel : lowCut)
        channel.fill({});
    for (auto& channel : bands)
        channel.fill({});
    bandEnv.fill(0.0);
    bandGain.fill(1.0);
    bandStep.fill(0.0);
    bandCount = 0;
    monoSide.fill({});
    std::fill(chorusL.begin(), chorusL.end(), 0.0f);
    std::fill(chorusR.begin(), chorusR.end(), 0.0f);
    chorusWrite = 0;
    chorusPhase = 0.0;
}

void MidFx::process(float* left, float* right, int n, const BassSettings& s) noexcept
{
    if (s.advFxDriveModel > 0)
    {
        BassSaturator::Params p;
        p.model = juce::jlimit(0, 6, s.advFxDriveModel);
        p.driveDb = s.advFxDriveDb;
        p.mix = s.advFxDriveMix;
        satL.process(left, n, p);
        satR.process(right, n, p);
    }
    if (s.lowCutHz > 0.5f)
    {
        // 24 dB / octave (two Butterworth stages), as Classic's Low Cut
        const double g = cutoffG(s.lowCutHz, sampleRate), k = std::sqrt(2.0);
        float* data[2] = { left, right };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double low = 0.0, band = 0.0, high = 0.0, x = data[c][i];
                for (auto& stage : lowCut[static_cast<size_t>(c)])
                {
                    svfTick(x, g, k, stage.ic1, stage.ic2, low, band, high);
                    x = high;
                }
                data[c][i] = static_cast<float>(x);
            }
    }
    if (s.advMultiband > 0.001f)
    {
        // three bands by Linkwitz-Riley crossovers (two Butterworth stages each) at 200 Hz and 2.5 kHz; each band's
        // level (stereo-linked) is pushed towards its target: 4:1 above, lifted up to +18 dB below (amount scales both)
        const double g1 = cutoffG(200.0, sampleRate), g2 = cutoffG(2500.0, sampleRate), k = std::sqrt(2.0);
        const double amount = juce::jlimit(0.0, 1.0, static_cast<double>(s.advMultiband));
        const double releaseMs = juce::jmax(2.0f, s.advMultibandMs);
        const double att = 1.0 - std::exp(-1.0 / (0.25 * releaseMs * 0.001 * sampleRate));
        const double rel = 1.0 - std::exp(-1.0 / (releaseMs * 0.001 * sampleRate));
        constexpr std::array<double, 3> target { -16.0, -20.0, -26.0 };
        for (int i = 0; i < n; ++i)
        {
            std::array<std::array<double, 3>, 2> band {};
            for (int c = 0; c < 2; ++c)
            {
                auto& st = bands[static_cast<size_t>(c)];
                const double x = c == 0 ? left[i] : right[i];
                double low = 0.0, bp = 0.0, high = 0.0, lo = 0.0, hi = 0.0;
                svfTick(x, g1, k, st[0].ic1, st[0].ic2, low, bp, high);
                svfTick(low, g1, k, st[1].ic1, st[1].ic2, lo, bp, high);
                svfTick(x, g1, k, st[2].ic1, st[2].ic2, low, bp, high);
                svfTick(high, g1, k, st[3].ic1, st[3].ic2, low, bp, hi);
                double midBand = 0.0, highBand = 0.0;
                svfTick(hi, g2, k, st[4].ic1, st[4].ic2, low, bp, high);
                svfTick(low, g2, k, st[5].ic1, st[5].ic2, midBand, bp, high);
                svfTick(hi, g2, k, st[6].ic1, st[6].ic2, low, bp, high);
                svfTick(high, g2, k, st[7].ic1, st[7].ic2, low, bp, highBand);
                band[static_cast<size_t>(c)] = { lo, midBand, highBand };
            }
            double outL = 0.0, outR = 0.0;
            for (size_t b = 0; b < 3; ++b)
            {
                const double peak = std::max(std::abs(band[0][b]), std::abs(band[1][b]));
                auto& e = bandEnv[b];
                e += (peak > e ? att : rel) * (peak - e);
                if (bandCount == 0)
                {
                    // the gain law every 16 samples, glided in between
                    const double levelDb = (e > 1.0e-6 ? 20.0 * std::log10(e) : -120.0);
                    double gainDb = 0.0;
                    if (levelDb > target[b])
                        gainDb = -(levelDb - target[b]) * 0.75;
                    else if (levelDb > -70.0)
                        gainDb = juce::jmin(18.0, (target[b] - levelDb) * 0.6);
                    bandStep[b] = (dbToGain(gainDb * amount) - bandGain[b]) / 16.0;
                }
                bandGain[b] += bandStep[b];
                outL += band[0][b] * bandGain[b];
                outR += band[1][b] * bandGain[b];
            }
            bandCount = (bandCount + 1) & 15;
            left[i] = static_cast<float>(outL);
            right[i] = static_cast<float>(outR);
        }
    }
    if (s.advChorusMix > 0.001f && chorusL.size() > 8)
    {
        const double base = 0.012 * sampleRate, depth = juce::jlimit(0.0f, 1.0f, s.advChorusDepth) * 0.006 * sampleRate;
        const double dt = juce::jlimit(0.01f, 8.0f, s.advChorusRateHz) / sampleRate;
        const double mix = juce::jlimit(0.0f, 1.0f, s.advChorusMix);
        const int size = static_cast<int>(chorusL.size());
        const auto read = [size](const std::vector<float>& line, double position)
        {
            const int i0 = static_cast<int>(std::floor(position));
            const double frac = position - i0;
            const auto at = [&](int i) { return static_cast<double>(line[static_cast<size_t>(((i % size) + size) % size)]); };
            return at(i0) + frac * (at(i0 + 1) - at(i0));
        };
        for (int i = 0; i < n; ++i)
        {
            chorusL[static_cast<size_t>(chorusWrite)] = left[i];
            chorusR[static_cast<size_t>(chorusWrite)] = right[i];
            const double dl = base + depth * (0.5 + 0.5 * std::sin(kTwoPi * chorusPhase));
            const double dr = base + depth * (0.5 + 0.5 * std::sin(kTwoPi * chorusPhase + 0.5 * kPi));
            const double wetL = read(chorusL, chorusWrite - dl), wetR = read(chorusR, chorusWrite - dr);
            left[i] = static_cast<float>((1.0 - 0.5 * mix) * left[i] + 0.5 * mix * wetL);
            right[i] = static_cast<float>((1.0 - 0.5 * mix) * right[i] + 0.5 * mix * wetR);
            chorusWrite = (chorusWrite + 1) % size;
            chorusPhase = wrap(chorusPhase + dt);
        }
    }
    if (s.advMonoHz > 1.0f)
    {
        // Mono Below: the side under the frequency goes (24 dB / octave), so the low mids stay in the middle as on the
        // references (side - mid -19 dB at 120-500 Hz) whatever the unison spread
        const double g = cutoffG(s.advMonoHz, sampleRate), k = std::sqrt(2.0);
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (left[i] + right[i]);
            double x = 0.5 * (left[i] - right[i]), low = 0.0, band = 0.0, high = 0.0;
            for (auto& stage : monoSide)
            {
                svfTick(x, g, k, stage.ic1, stage.ic2, low, band, high);
                x = high;
            }
            left[i] = static_cast<float>(m + x);
            right[i] = static_cast<float>(m - x);
        }
    }
}
} // namespace bbg::adv
