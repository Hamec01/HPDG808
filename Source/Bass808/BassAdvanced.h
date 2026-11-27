#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Bass808/BassSaturator.h"
#include "Bass808/BassSettings.h"
#include "Bass808/BassWavetable.h"

namespace bbg::adv
{
// HPDG 808's Advanced engine (Bass Architecture 2.0, P2 / P3; docs/bass-architecture/WORKPLAN.md):
//
//   OSC A / B / C   Sine, Triangle, Saw, Square, Pulse (PolyBLEP); octave, semitones, fine, level, pan; unison
//                   1-7 voices (detune, stereo spread, 1 / sqrt(n) level); phase / random phase; PM from a
//                   higher-index oscillator (which may be silent: modulation only); Shape = width / octave saw / fold
//   SUB             mono sine / triangle, -2 .. 0 octaves, its own warmth; never filtered, widened or FX'd
//   NOISE           stereo white into the mids' filter
//   -> DRIVE (tanh, peak kept) -> FILTER A (LP12 / LP24 / HP12 / BP, key, env) -> FILTER B (series / parallel)
//   -> WIDTH (M / S) -> AMP ENV (ADSR; its release is the voice's fade)
//   MOD             ENV 2 (filter), ENV 3 (mod), 4 LFOs (sine .. smooth random, free / tempo sync), velocity, note,
//                   8 matrix slots into pitch, levels, shapes, PM, cutoffs, resonance, drive, detune, width, amp
//   MID FX (engine) HamloMag saturator -> Low Cut -> 3-band up / down compressor (OTT-like) -> chorus
//
// Bass only: one voice sounds (the engine keeps Classic's mono note lifecycle); no allocation while rendering.
struct Env
{
    enum Stage { Off, Attack, Decay, Sustain, Release };
    Stage stage = Off;
    double level = 0.0, attackStep = 1.0, decayCoef = 0.0, releaseCoef = 0.0, sustain = 1.0;
    void set(const BassSettings::AdvEnv&, double sampleRate) noexcept;
    void start() noexcept { stage = Attack; }
    void release() noexcept { if (stage != Off) stage = Release; }
    double tick() noexcept;
};

struct Lfo
{
    double phase = 0.0, held = 0.0, from = 0.0, to = 0.0;
    std::uint32_t seed = 0x2545f491u;
    double tick(int shape, double dt) noexcept;
    double random() noexcept;
};

class Voice
{
public:
    static constexpr int kMaxUnison = 7;
    void prepare(double sampleRate) noexcept;
    void setSettings(const BassSettings&) noexcept;
    // at the note: phases, envelopes, filters from the start
    void start(float velocity, int note, double tempo) noexcept;
    void release() noexcept;
    void setTempo(double bpm) noexcept { tempo = bpm > 20.0 ? bpm : 120.0; }
    // one sample at `hz` (the played pitch with its glide); adds the sub and the mids
    void tick(double hz, double gain, float& sub, float& left, float& right) noexcept;
    bool finished() const noexcept { return amp.stage == Env::Off; }

private:
    static constexpr int kControl = 16; // samples per modulation step
    struct Osc
    {
        std::array<double, kMaxUnison> phase {};
    };
    struct Ramp
    {
        double now = 0.0, target = 0.0, step = 0.0;
    };
    struct OscControl
    {
        bool on = false;
        int voices = 1;
        std::array<double, kMaxUnison> dt {}, panL {}, panR {};
        double norm = 1.0, shape = 0.0, pm = 0.0;
        int table = 0;
        Ramp level, wtPos;
    };
    struct Control
    {
        std::array<OscControl, 3> osc {};
        Ramp noise, gA, kA, gB, kB, width, mid, sub;
        bool driveOn = false, bypassA = true, started = false;
        double driveG = 1.0, driveNorm = 1.0, subDt = 0.0, subWarm = 1.0, subWarmNorm = 1.0;
        std::array<Ramp*, 14> ramps() noexcept
        {
            return { &osc[0].level, &osc[1].level, &osc[2].level, &osc[0].wtPos, &osc[1].wtPos, &osc[2].wtPos,
                     &noise, &gA, &kA, &gB, &kB, &width, &mid, &sub };
        }
    };
    double oscSample(int wave, double t, double dt, double shape) const noexcept;
    double lfoHz(const BassSettings::AdvLfo&) const noexcept;
    void updateControl(double hz) noexcept;

    BassSettings s;
    double sampleRate = 48000.0, tempo = 120.0;
    float velocity = 1.0f;
    int note = 48;
    std::array<Osc, 3> osc {};
    double subPhase = 0.0;
    Env amp, filterEnv, modEnv;
    std::array<Lfo, 4> lfo {};
    std::array<std::array<double, 4>, 2> svfA {}, svfB {}; // [channel][ic1, ic2 per stage x 2]
    std::uint32_t noiseSeed = 0x9e3779b9u;
    const wt::Bank* tables = nullptr; // built in prepare(), never on the audio thread
    Control ctl;
    int controlLeft = 0;
};

class MidFx
{
public:
    void prepare(double sampleRate, int maxBlock);
    void reset() noexcept;
    void process(float* left, float* right, int numSamples, const BassSettings&) noexcept;

private:
    struct Stage
    {
        double ic1 = 0.0, ic2 = 0.0;
    };
    double sampleRate = 48000.0;
    BassSaturator satL, satR;
    std::array<std::array<Stage, 2>, 2> lowCut {};               // [channel][stage]
    std::array<std::array<Stage, 8>, 2> bands {};                // [channel][LR4 crossovers]
    std::array<double, 3> bandEnv {}, bandGain { 1.0, 1.0, 1.0 }, bandStep {};
    int bandCount = 0;
    std::array<Stage, 2> monoSide {};                            // Mono Below: the side's high-pass (2 stages)
    std::vector<float> chorusL, chorusR;
    int chorusWrite = 0;
    double chorusPhase = 0.0;
};
} // namespace bbg::adv
