#pragma once

#include <array>
#include <memory>
#include <juce_core/juce_core.h>

namespace bbg
{
class BassDrawWave;

// Every control of HPDG 808's sound (the engine in BassEngine.h). Plain data: HPDG's Sub808 lane keeps one,
// the plugin builds one from its parameters, BassSettingsIO.h reads and writes it.
struct BassSettings
{
    enum Wave { Sine = 0, Triangle, Saw, Square, Reese, NumWaves };
    enum FilterType { LP12 = 0, LP24, HP12, BP, Vowel, NumFilters };
    enum PlayMode { Retrigger = 0, Legato, Glide };
    static constexpr float kOscBOffDb = -60.0f;

    // synth layer
    int wave = Sine;
    bool drawWaveEnabled = false;
    std::shared_ptr<const BassDrawWave> drawnWave;
    float shape = 0.0f;           // Sine: FM depth; Triangle: skew; Saw: second saw detune; Square: pulse width; Reese: detune
    float sub = 0.0f;             // a sine under the oscillator
    int subOctave = 0;            // 0: one octave down, 1: at the note (under a Reese / saw, where -1 octave is below hearing)
    float synthLevelDb = 0.0f;    // -60: off
    float punchSemitones = 18.0f;
    float punchMs = 40.0f;
    float drive = 4.5f;           // 808 saturation (odd), bias (even), wet blend
    float bias = 0.08f;
    float saturation = 0.8f;
    float toneHz = 800.0f;        // key-tracked 4th-order low-pass at C2; >= 15 kHz: open
    float toneFallOct = 0.0f;     // a gentle (6 dB / oct) low-pass from the 16th harmonic down, closing this many octaves a
                                  // second (to 1.5 x the note): the upper harmonics fade faster than the fundamental, as on a
                                  // string, whose losses rise with frequency (docs/audit/BASS808_FIT.md)
    float clickDb = -30.0f;       // <= -60: off
    float boostDb = 0.0f;

    // OSC B follows the played pitch and adds only harmonics above the fundamental.
    float oscBLevelDb = kOscBOffDb;
    float oscBShape = 0.0f;
    float oscBDecayMs = 180.0f;   // time for the transient harmonics to fall by 6 dB
    float oscBSustain = 0.0f;     // harmonic level remaining relative to the voice envelope

    // envelope
    float attackMs = 2.0f;
    float swellDb = 0.0f;         // after the attack the level starts this far under full and rises to it over Swell Time
    float swellMs = 20.0f;        // (a time constant): the references reach 90 % of their peak at ~16 ms, half at once
    float holdSeconds = 0.05f;
    float decaySeconds = 0.32f;   // -6 dB per this long
    float sustain = 0.0f;         // level the decay settles at while the key is held
    float releaseMs = 30.0f;

    // sample layer
    float sampleLevelDb = -60.0f; // -60: off
    float sampleTune = 0.0f;      // semitones
    float sampleFine = 0.0f;      // cents
    float sampleStart = 0.0f;     // fractions of the file
    float sampleEnd = 1.0f;
    bool sampleLoop = false;
    float sampleLoopStart = 0.5f;
    float sampleStretch = 1.0f;   // length x (0.25 .. 4)
    bool sampleLockTime = false;  // true: the same length on every note (pitch moves, time does not)
    bool sampleFollow = true;     // false: every key plays the sample at its own pitch
    bool sampleReverse = false;
    bool sampleInvert = false;
    float handoffMs = 0.0f;       // 0: layered; else the sample plays this long, then crossfades into the synth

    // filter
    int filterType = LP12;
    float cutoffHz = 20000.0f;
    float resonance = 0.0f;
    float filterEnvOct = 0.0f;
    float filterDecayMs = 200.0f; // to 10 %
    float filterKey = 0.0f;
    float vowel = 0.0f;           // 0 A, 1 E, 2 I, 3 O, 4 U

    // LFO -> cutoff (octaves) / vowel (steps)
    float lfoRateHz = 1.0f;
    int lfoSync = 0;              // 0 free, then 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T 1/16T
    float lfoDepth = 0.0f;
    bool lfoRetrigger = true;

    // saturator (BassSaturator)
    int satModel = 0;             // 0 off, 1 Classic, 2 Tape, 3 Tube, 4 Iron, 5 Germanium, 6 Vinyl
    float satDriveDb = 0.0f;
    float satHarmonics = 0.5f;
    float satEvenOdd = 0.0f;
    float satMix = 1.0f;

    float phantom = 0.0f;

    // bus filter after the saturator (HPDG's lane strip): -1 .. 0 closes the highs 20 kHz -> 200 Hz,
    // 0 .. 1 opens a low cut 20 -> 400 Hz; 0: off
    float busFilter = 0.0f;
    // the preset's own low cut after the saturator (24 dB / octave), Hz; 0: off. A mid bass that plays over a separate
    // sub (DnB Dancefloor's Mid A / Mid B lanes) keeps its lows out of the sub's band (the lane's FILTER is the user's).
    float lowCutHz = 0.0f;

    // master (BassMaster, the HPDG 808 plugin only; the defaults pass the sound untouched)
    int masterSub = 0;               // 0 off, 1 Cut, 2 Boost
    float masterSubHz = 30.0f;       // 10 .. 150
    float masterFreqHz = 150.0f;     // under it: mono, GAIN; above it: WIDTH
    float masterLowGainDb = 0.0f;    // -10 .. 10
    float masterWidth = 0.0f;        // %, 0 .. 200
    int masterHarmType = 0;          // 0 2nd / 3rd, 1 even / odd, 2 crunchy
    float masterHarmonics = 0.0f;    // 0 .. 1
    float masterHarmSide = 0.5f;     // 0 middle .. 1 sides
    float masterDampenHz = 2000.0f;  // low-pass on the harmonics
    float masterThresholdDb = -12.0f;
    float masterReleaseMs = 150.0f;
    float masterDynamics = 0.0f;     // -1 compress .. 1 expand
    bool masterLimit = false;

    // ---- Advanced engine (Bass Architecture 2.0, docs/bass-architecture/WORKPLAN.md). engine = Classic ignores
    // every adv field, so a Classic sound stays bit-identical. Advanced shares the note lifecycle (held stack,
    // Glide / Legato, the 3 ms choke), the bus filter, Low Cut (on its mids only) and the gain with Classic.
    enum Engine { Classic = 0, Advanced };
    enum AdvWave { AdvSine = 0, AdvTriangle, AdvSaw, AdvSquare, AdvPulse, AdvWavetable, NumAdvWaves };
    enum AdvModSource { ModNone = 0, ModLfo1, ModLfo2, ModLfo3, ModLfo4, ModFilterEnv, ModModEnv, ModAmpEnv, ModVelocity,
                        ModNote, NumModSources };
    enum AdvModDest { DestNone = 0, DestPitch, DestOscAPitch, DestOscBPitch, DestOscCPitch, DestOscALevel, DestOscBLevel,
                      DestOscCLevel, DestOscAShape, DestOscBShape, DestOscCShape, DestOscAPm, DestOscBPm, DestOscCPm,
                      DestCutoffA, DestResoA, DestCutoffB, DestDrive, DestDetune, DestNoise, DestSub, DestWidth, DestAmp,
                      DestOscAWt, DestOscBWt, DestOscCWt, NumModDests }; // append only: saved slots hold the numbers
    struct AdvOsc
    {
        bool on = false;
        int wave = AdvSaw;
        int octave = 0;           // -3 .. 3
        float semitones = 0.0f;   // -24 .. 24
        float fine = 0.0f;        // cents
        float levelDb = -6.0f;
        float pan = 0.0f;         // -1 .. 1
        int unison = 1;           // 1 .. 7 voices
        float detune = 0.25f;     // 0 .. 1: the outer voices this share of 50 cents away
        float spread = 0.5f;      // 0 .. 1: the outer voices this far to the sides
        float shape = 0.0f;       // Pulse: width; Saw: a saw an octave up mixed in; Sine / Triangle: fold
        float phase = 0.0f;       // 0 .. 1, at the note (randomPhase off)
        bool randomPhase = true;  // unison voices start anywhere (a real stack); voice 1 keeps `phase`
        int pmFrom = -1;          // -1: none, else an oscillator of a HIGHER index (C -> A / B, B -> A: acyclic)
        float pmAmount = 0.0f;    // 0 .. 1 (index up to 4 radians)
        bool audible = true;      // off: only modulates (PM)
        int table = 0;            // Wavetable: BassWavetable.h's tables (0 Basic Shapes, 1 Kick Pulse, ...)
        float wtPos = 0.0f;       // Wavetable: the Position along the table, 0 .. 1
    };
    struct AdvEnv
    {
        float attackMs = 2.0f, decayMs = 300.0f, sustain = 1.0f, releaseMs = 60.0f;
    };
    struct AdvLfo
    {
        int shape = 0;            // 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square, 5 sample & hold, 6 smooth random
        float rateHz = 2.0f;
        int sync = 0;             // as lfoSync: 0 free, then 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T 1/16T
        bool retrigger = true;
    };
    struct AdvMod
    {
        int source = ModNone, dest = DestNone;
        float amount = 0.0f;      // -1 .. 1 of the destination's range (pitch 24 st, cutoff 6 oct, levels 24 dB, ...)
    };
    int engine = Classic;
    std::array<AdvOsc, 3> advOsc {};
    // the sub: mono, its own sine / triangle, under the mids and past their FX
    bool advSubOn = false;
    int advSubWave = 0;           // 0 sine, 1 triangle
    int advSubOctave = -1;        // -2 .. 0 from the played note
    float advSubLevelDb = -6.0f;
    float advSubWarm = 0.0f;      // 0 .. 1 soft saturation of the sub alone
    float advSubH3 = 0.0f;        // the sub's 3rd / 5th harmonic, 0 .. 1 of its fundamental (exact sines: no aliasing;
    float advSubH5 = 0.0f;        // Dimension's sub: 67 / 33 / 4 % -> H3 0.49, H5 0.06)
    float advNoiseDb = -60.0f;    // white noise into the mids' filter (attack, texture); -60 off
    // filters (the mids): A, B in series or parallel
    int advFilterAType = LP24;    // LP12, LP24, HP12, BP
    float advCutoffA = 20000.0f, advResoA = 0.0f, advKeyA = 0.0f, advEnvA = 0.0f; // env: octaves of the filter env
    float advDriveDb = 0.0f;      // before filter A (0 .. 24 dB, tanh with make-up)
    bool advFilterBOn = false;
    int advFilterBType = HP12;
    float advCutoffB = 20.0f, advResoB = 0.0f;
    bool advFiltersParallel = false;
    AdvEnv advAmpEnv {}, advFilterEnv { 1.0f, 250.0f, 0.0f, 120.0f }, advModEnv { 1.0f, 250.0f, 0.0f, 120.0f };
    std::array<AdvLfo, 4> advLfo {};
    std::array<AdvMod, 8> advMod {};
    // mid FX (after the voices, stereo)
    int advFxDriveModel = 0;      // the saturator's models (0 off)
    float advFxDriveDb = 0.0f, advFxDriveMix = 1.0f;
    float advMultiband = 0.0f;    // 0 .. 1: three bands, each pressed down above and lifted below its target (OTT-like)
    float advMultibandMs = 30.0f;
    float advChorusMix = 0.0f, advChorusRateHz = 0.6f, advChorusDepth = 0.5f;
    float advWidth = 1.0f;        // 0 mono .. 2: the mids' side level
    float advMonoHz = 0.0f;       // the mids' side under this is taken out (12 dB / oct): a mono low-mid; 0 off
    float advMidDb = 0.0f;        // the mids' level against the sub

    // play
    int mode = Glide;
    float glideMs = 120.0f;
    float tuneSemitones = 0.0f;
    float gainDb = 0.0f;
    float velocity = 0.5f;
};
} // namespace bbg
