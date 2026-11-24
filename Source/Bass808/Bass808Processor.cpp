#include "Bass808Processor.h"

#include "Bass808Editor.h"
#include "BassDrawWave.h"
#include "Bass808Widgets.h"
#include "BassPresetLibrary.h"

#include <string_view>

namespace bbg
{
namespace
{
// Parameter ids are stable (saved in DAW projects). Version 1: the first HPDG 808; version 2: the
// layers, filter, LFO, saturator and phantom; version 3: the master; version 4: OSC B.
struct FloatParam
{
    const char* id;
    const char* name;
    float BassSettings::*field;
    float low, high, centre, step; // centre 0: linear
    const char* unit;
    int version;
};
struct ChoiceParam
{
    const char* id;
    const char* name;
    int BassSettings::*field;
    const char* choices;
    int version;
};
struct BoolParam
{
    const char* id;
    const char* name;
    bool BassSettings::*field;
    int version;
};

const std::vector<FloatParam>& floatParams()
{
    static const std::vector<FloatParam> list {
        // ranges widened 2026-11-15 by the measured one-shots (docs/audit/BASS808_FIT.md): punches reach 48 st
        { "punch", "Punch", &BassSettings::punchSemitones, 0.0f, 48.0f, 18.0f, 0.1f, "st", 1 },
        { "punchTime", "Punch Time", &BassSettings::punchMs, 2.0f, 200.0f, 30.0f, 0.0f, "ms", 1 },
        { "drive", "Drive", &BassSettings::drive, 0.5f, 10.0f, 3.0f, 0.0f, "", 1 },
        { "even", "Even", &BassSettings::bias, 0.0f, 0.3f, 0.0f, 0.005f, "", 1 },
        { "saturation", "Saturation", &BassSettings::saturation, 0.0f, 1.0f, 0.0f, 0.01f, "", 1 },
        { "tone", "Tone", &BassSettings::toneHz, 100.0f, 16000.0f, 900.0f, 0.0f, "Hz", 1 },
        { "hold", "Hold", &BassSettings::holdSeconds, 0.0f, 3.0f, 0.4f, 0.0f, "s", 1 },
        { "decay", "Decay", &BassSettings::decaySeconds, 0.01f, 4.0f, 0.4f, 0.0f, "s", 1 },
        { "attack", "Attack", &BassSettings::attackMs, 0.1f, 20.0f, 2.0f, 0.0f, "ms", 1 },
        { "click", "Click", &BassSettings::clickDb, -60.0f, -10.0f, 0.0f, 0.5f, "dB", 1 },
        { "boost", "Boost", &BassSettings::boostDb, 0.0f, 12.0f, 0.0f, 0.1f, "dB", 1 },
        { "glide", "Glide", &BassSettings::glideMs, 0.0f, 500.0f, 100.0f, 0.0f, "ms", 1 },
        { "release", "Release", &BassSettings::releaseMs, 2.0f, 1000.0f, 60.0f, 0.0f, "ms", 1 },
        { "tune", "Tune", &BassSettings::tuneSemitones, -12.0f, 12.0f, 0.0f, 1.0f, "st", 1 },
        { "gain", "Gain", &BassSettings::gainDb, -24.0f, 6.0f, 0.0f, 0.1f, "dB", 1 },
        { "velocity", "Velocity", &BassSettings::velocity, 0.0f, 1.0f, 0.0f, 0.01f, "", 1 },
        { "shape", "Shape", &BassSettings::shape, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "sub", "Sub", &BassSettings::sub, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "synthLevel", "Synth", &BassSettings::synthLevelDb, -60.0f, 6.0f, 0.0f, 0.1f, "dB", 2 },
        { "sustain", "Sustain", &BassSettings::sustain, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpLevel", "Sample", &BassSettings::sampleLevelDb, -60.0f, 6.0f, 0.0f, 0.1f, "dB", 2 },
        { "smpTune", "Sample Tune", &BassSettings::sampleTune, -24.0f, 24.0f, 0.0f, 1.0f, "st", 2 },
        { "smpFine", "Sample Fine", &BassSettings::sampleFine, -100.0f, 100.0f, 0.0f, 1.0f, "ct", 2 },
        { "smpStart", "Sample Start", &BassSettings::sampleStart, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpEnd", "Sample End", &BassSettings::sampleEnd, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpLoopStart", "Loop Start", &BassSettings::sampleLoopStart, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "smpStretch", "Stretch", &BassSettings::sampleStretch, 0.25f, 4.0f, 1.0f, 0.0f, "x", 2 },
        { "handoff", "Handoff", &BassSettings::handoffMs, 0.0f, 500.0f, 60.0f, 0.0f, "ms", 2 },
        { "cutoff", "Cutoff", &BassSettings::cutoffHz, 20.0f, 20000.0f, 600.0f, 0.0f, "Hz", 2 },
        { "reso", "Resonance", &BassSettings::resonance, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "fltEnv", "Filter Env", &BassSettings::filterEnvOct, -4.0f, 4.0f, 0.0f, 0.0f, "oct", 2 },
        { "fltDecay", "Filter Decay", &BassSettings::filterDecayMs, 5.0f, 2000.0f, 200.0f, 0.0f, "ms", 2 },
        { "fltKey", "Key Track", &BassSettings::filterKey, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "vowel", "Vowel", &BassSettings::vowel, 0.0f, 4.0f, 0.0f, 0.0f, "", 2 },
        { "lfoRate", "LFO Rate", &BassSettings::lfoRateHz, 0.05f, 20.0f, 2.0f, 0.0f, "Hz", 2 },
        { "lfoDepth", "LFO Depth", &BassSettings::lfoDepth, 0.0f, 4.0f, 0.0f, 0.0f, "oct", 2 },
        { "satDrive", "Sat Drive", &BassSettings::satDriveDb, 0.0f, 36.0f, 0.0f, 0.1f, "dB", 2 },
        { "satHarm", "Harmonics", &BassSettings::satHarmonics, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "satEvenOdd", "Even/Odd", &BassSettings::satEvenOdd, -1.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "satMix", "Sat Mix", &BassSettings::satMix, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "phantom", "Phantom", &BassSettings::phantom, 0.0f, 1.0f, 0.0f, 0.0f, "", 2 },
        { "toneFall", "Tone Fall", &BassSettings::toneFallOct, 0.0f, 8.0f, 1.0f, 0.0f, "oct/s", 3 },
        { "swell", "Swell", &BassSettings::swellDb, 0.0f, 24.0f, 0.0f, 0.1f, "dB", 3 },
        { "swellTime", "Swell Time", &BassSettings::swellMs, 2.0f, 200.0f, 20.0f, 0.0f, "ms", 3 },
        { "mSubHz", "Sub Freq", &BassSettings::masterSubHz, 10.0f, 150.0f, 40.0f, 0.0f, "Hz", 3 },
        { "mFreq", "Master Freq", &BassSettings::masterFreqHz, 40.0f, 2500.0f, 250.0f, 0.0f, "Hz", 3 },
        { "mLowGain", "Low Gain", &BassSettings::masterLowGainDb, -10.0f, 10.0f, 0.0f, 0.1f, "dB", 3 },
        { "mWidth", "Width", &BassSettings::masterWidth, 0.0f, 200.0f, 0.0f, 1.0f, "%", 3 },
        { "mHarm", "Harmonics Amount", &BassSettings::masterHarmonics, 0.0f, 1.0f, 0.0f, 0.0f, "", 3 },
        { "mHarmSide", "Harmonics Mid/Side", &BassSettings::masterHarmSide, 0.0f, 1.0f, 0.0f, 0.0f, "", 3 },
        { "mDampen", "Harmonics Dampen", &BassSettings::masterDampenHz, 200.0f, 8000.0f, 1500.0f, 0.0f, "Hz", 3 },
        { "mThresh", "Threshold", &BassSettings::masterThresholdDb, -60.0f, 0.0f, 0.0f, 0.1f, "dB", 3 },
        { "mRelease", "Dyn Release", &BassSettings::masterReleaseMs, 20.0f, 1000.0f, 150.0f, 0.0f, "ms", 3 },
        { "mDyn", "Dynamics", &BassSettings::masterDynamics, -1.0f, 1.0f, 0.0f, 0.01f, "", 3 },
        { "oscBLevel", "OSC B Level", &BassSettings::oscBLevelDb, BassSettings::kOscBOffDb, 0.0f, 0.0f, 0.1f, "dB", 4 },
        { "oscBShape", "OSC B Shape", &BassSettings::oscBShape, 0.0f, 1.0f, 0.0f, 0.0f, "", 4 },
        { "oscBDecay", "OSC B Decay", &BassSettings::oscBDecayMs, 5.0f, 2000.0f, 180.0f, 0.0f, "ms", 4 },
        { "oscBSustain", "OSC B Sustain", &BassSettings::oscBSustain, 0.0f, 1.0f, 0.0f, 0.0f, "", 4 },
    };
    return list;
}

const std::vector<ChoiceParam>& choiceParams()
{
    static const std::vector<ChoiceParam> list {
        { "mode", "Mode", &BassSettings::mode, "Retrigger,Legato,Glide", 1 },
        { "wave", "Wave", &BassSettings::wave, "Sine,Triangle,Saw,Square,Reese", 2 },
        { "subOct", "Sub Octave", &BassSettings::subOctave, "-1 oct,0 oct", 2 },
        { "fltType", "Filter", &BassSettings::filterType, "LP12,LP24,HP12,BP,Vowel", 2 },
        { "lfoSync", "LFO Sync", &BassSettings::lfoSync, "Free,1/1,1/2,1/4,1/8,1/16,1/4T,1/8T,1/16T", 2 },
        { "satModel", "Saturator", &BassSettings::satModel, "Off,Classic,Tape,Tube,Iron,Germanium,Vinyl", 2 },
        { "mSub", "Sub Filter", &BassSettings::masterSub, "Off,Cut,Boost", 3 },
        { "mHarmType", "Harmonics Type", &BassSettings::masterHarmType, "2nd/3rd,Ev/Odd,Crunchy", 3 },
    };
    return list;
}

const std::vector<BoolParam>& boolParams()
{
    static const std::vector<BoolParam> list {
        { "smpLoop", "Loop", &BassSettings::sampleLoop, 2 },
        { "smpLock", "Lock Time", &BassSettings::sampleLockTime, 2 },
        { "smpFollow", "Follow Key", &BassSettings::sampleFollow, 2 },
        { "smpReverse", "Reverse", &BassSettings::sampleReverse, 2 },
        { "smpInvert", "Invert", &BassSettings::sampleInvert, 2 },
        { "lfoRetrig", "LFO Retrigger", &BassSettings::lfoRetrigger, 2 },
        { "mLimit", "Limit", &BassSettings::masterLimit, 3 },
        { "oscADraw", "Draw Wave", &BassSettings::drawWaveEnabled, 5 },
    };
    return list;
}

juce::String valueText(const FloatParam& p, float value)
{
    if (p.field == &BassSettings::oscBLevelDb && value <= BassSettings::kOscBOffDb)
        return "Off";
    if (std::string_view(p.id) == "click" && value <= -59.5f)
        return "off";
    if ((std::string_view(p.id) == "synthLevel" || std::string_view(p.id) == "smpLevel") && value <= -59.5f)
        return "off";
    if (std::string_view(p.id) == "handoff" && value < 0.5f)
        return "layered";
    if (std::string_view(p.id) == "smpStart" || std::string_view(p.id) == "smpEnd" || std::string_view(p.id) == "smpLoopStart")
        return juce::String(juce::roundToInt(value * 100.0f)) + " %";
    if (std::string_view(p.id) == "mHarm")
        return value < 0.005f ? juce::String("off") : juce::String(juce::roundToInt(value * 100.0f)) + " %";
    if (std::string_view(p.id) == "mHarmSide")
        return value < 0.02f ? juce::String("mid") : (value > 0.98f ? juce::String("side") : juce::String(juce::roundToInt(value * 100.0f)) + " % side");
    if (std::string_view(p.id) == "mDyn")
        return std::abs(value) < 0.005f ? juce::String("off")
                                        : juce::String(value < 0.0f ? "comp " : "xpnd ") + juce::String(juce::roundToInt(std::abs(value) * 100.0f)) + " %";
    if (std::string_view(p.id) == "mWidth")
        return value < 0.5f ? juce::String("mono") : juce::String(juce::roundToInt(value)) + " %";
    if (std::string_view(p.id) == "vowel")
    {
        static const char* vowels[] { "A", "E", "I", "O", "U" };
        const int i = juce::jlimit(0, 3, static_cast<int>(value));
        const float f = value - static_cast<float>(i);
        return f < 0.05f ? juce::String(vowels[i]) : (f > 0.95f ? juce::String(vowels[i + 1]) : juce::String(vowels[i]) + "-" + vowels[i + 1]);
    }
    return b808::formatValue(value, p.unit);
}
} // namespace

const std::vector<BassPreset>& Bass808Processor::programs()
{
    static const auto presets = []
    {
        auto bank = bassPresets();
        // Original voicings. Keep these after the released programs, whose indices are saved in projects.
        struct Voicing
        {
            const char* name;
            float punch, punchMs, attackMs, hold, decay, sustain, releaseMs;
            float tone, drive, bias, saturation, click;
            float bLevel, bShape, bDecay, bSustain;
            int satModel;
            float satDrive, satMix, gain;
            int mode;
            float glideMs;
            float swellDb = 0.0f;
            float swellMs = 20.0f;
            float synthLevelDb = 0.0f;
        };
        // Each row starts with pitch and amp articulation, then A tone and B colour,
        // followed by saturation, output gain and play mode.
        constexpr Voicing voicings[] {
            { "Clean - Still Air", 0.0f, 12, 6, .08f, .55f, .24f, 85,
              700, 1.1f, .00f, .02f, -60,  -24, .05f, 430, .65f,
              0, 0, .00f, -3.0f, BassSettings::Retrigger, 0 },
            { "Clean - Warm Floor", 2.4f, 14, 4, .04f, .36f, .12f, 55,
              530, 2.3f, .11f, .28f, -60,  -15, .12f, 220, .29f,
              2, 2, .20f, -4.0f, BassSettings::Retrigger, 0 },
            { "Clean - Soft Bloom", 1.0f, 30, 32, .14f, .52f, .25f, 140,
              620, 1.8f, .05f, .18f, -60,  -22, .24f, 560, .38f,
              0, 0, .00f, -3.5f, BassSettings::Legato, 0, 11, 90 },
            { "Punch - Knock", 24.0f, 11, 1, .00f, .13f, .00f, 28,
              1350, 3.0f, .06f, .27f, -37,  -10, .65f, 36, .02f,
              0, 0, .00f, -4.5f, BassSettings::Retrigger, 0 },
            { "Punch - Thud", 13.0f, 19, 2, .02f, .21f, .00f, 38,
              650, 3.5f, .04f, .37f, -51,  -12, .21f, 71, .05f,
              2, 4, .34f, -4.5f, BassSettings::Retrigger, 0 },
            { "Punch - Snap", 31.0f, 8, .8f, .00f, .09f, .00f, 24,
              2280, 2.0f, .13f, .23f, -33,  -9, .92f, 24, .00f,
              1, 3, .38f, -5.8f, BassSettings::Retrigger, 0, 0, 20, -3.0f },
            { "Grit - Burnt Edge", 10.0f, 18, 3, .05f, .42f, .13f, 70,
              1130, 3.4f, .16f, .39f, -48,  -7, .86f, 170, .56f,
              5, 9, .52f, -6.5f, BassSettings::Retrigger, 0 },
            { "Grit - Iron Bark", 16.0f, 23, 2, .02f, .25f, .04f, 52,
              1740, 4.2f, .07f, .36f, -47,  -5, .99f, 59, .25f,
              4, 11, .60f, -8.0f, BassSettings::Retrigger, 0 },
            { "Grit - Black Coil", 4.0f, 22, 11, .07f, .52f, .29f, 120,
              900, 2.7f, .08f, .47f, -60,  -6, .52f, 560, .84f,
              3, 8, .55f, -6.5f, BassSettings::Legato, 0 },
            { "Slide - Low Rail", 3.0f, 19, 4, .07f, .68f, .52f, 115,
              820, 2.0f, .04f, .18f, -60,  -15, .31f, 390, .72f,
              0, 0, .00f, -4.0f, BassSettings::Glide, 110 },
            { "Slide - Wire", 6.0f, 15, 3, .03f, .47f, .47f, 130,
              1130, 2.8f, .09f, .29f, -60,  -7, .93f, 310, .82f,
              2, 4, .32f, -5.5f, BassSettings::Glide, 175 },
            { "Slide - Bent Steel", 14.0f, 29, 7, .10f, .81f, .63f, 165,
              1550, 3.4f, .12f, .33f, -50,  -9, .70f, 120, .54f,
              4, 7, .45f, -6.0f, BassSettings::Glide, 245 },
        };
        for (const auto& v : voicings)
        {
            BassSettings s;
            s.punchSemitones = v.punch; s.punchMs = v.punchMs; s.attackMs = v.attackMs;
            s.holdSeconds = v.hold; s.decaySeconds = v.decay; s.sustain = v.sustain; s.releaseMs = v.releaseMs;
            s.toneHz = v.tone; s.drive = v.drive; s.bias = v.bias; s.saturation = v.saturation; s.clickDb = v.click;
            s.oscBLevelDb = v.bLevel; s.oscBShape = v.bShape; s.oscBDecayMs = v.bDecay; s.oscBSustain = v.bSustain;
            s.satModel = v.satModel; s.satDriveDb = v.satDrive; s.satMix = v.satMix; s.gainDb = v.gain;
            s.mode = v.mode; s.glideMs = v.glideMs;
            s.swellDb = v.swellDb; s.swellMs = v.swellMs;
            s.synthLevelDb = v.synthLevelDb;
            bank.push_back({ "Trap / Drill", v.name, s });
        }
        return bank;
    }();
    return presets;
}

Bass808Processor::Bass808Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HPDG808", createLayout())
{
    applyPreset(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout Bass808Processor::createLayout()
{
    const BassSettings defaults;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : floatParams())
    {
        juce::NormalisableRange<float> range(p.low, p.high, p.step);
        if (p.centre > 0.0f)
            range.setSkewForCentre(p.centre);
        const FloatParam copy = p;
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { p.id, p.version }, p.name, range, defaults.*(p.field),
            juce::AudioParameterFloatAttributes()
                .withLabel(p.unit)
                .withStringFromValueFunction([copy](float value, int) { return valueText(copy, value); })));
    }
    for (const auto& p : choiceParams())
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { p.id, p.version }, p.name,
                                                                juce::StringArray::fromTokens(p.choices, ",", ""), defaults.*(p.field)));
    for (const auto& p : boolParams())
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { p.id, p.version }, p.name, defaults.*(p.field)));
    return layout;
}

BassSettings Bass808Processor::currentSettings() const
{
    BassSettings s;
    for (const auto& p : floatParams())
        s.*(p.field) = apvts.getRawParameterValue(p.id)->load();
    for (const auto& p : choiceParams())
        s.*(p.field) = juce::roundToInt(apvts.getRawParameterValue(p.id)->load());
    for (const auto& p : boolParams())
        s.*(p.field) = apvts.getRawParameterValue(p.id)->load() >= 0.5f;
    s.drawnWave = getDrawWave();
    return s;
}

void Bass808Processor::setParameter(const char* id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void Bass808Processor::writeSettings(const BassSettings& s)
{
    setDrawWave(s.drawnWave);
    for (const auto& p : floatParams())
        setParameter(p.id, s.*(p.field));
    for (const auto& p : choiceParams())
        setParameter(p.id, static_cast<float>(s.*(p.field)));
    for (const auto& p : boolParams())
        setParameter(p.id, s.*(p.field) ? 1.0f : 0.0f);
}

void Bass808Processor::applySectionPreset(int section, const BassSettings& s)
{
    const auto ids = bassSectionParameterIds(section);
    if (ids.isEmpty()) return;
    if (section == 0) setDrawWave(s.drawnWave);
    const auto set = [&](const char* id, float value)
    {
        if (!ids.contains(id)) return;
        if (auto* parameter = apvts.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    for (const auto& p : floatParams()) set(p.id, s.*(p.field));
    for (const auto& p : choiceParams()) set(p.id, static_cast<float>(s.*(p.field)));
    for (const auto& p : boolParams()) set(p.id, s.*(p.field) ? 1.0f : 0.0f);
}

const juce::String Bass808Processor::getProgramName(int index)
{
    const auto& list = programs();
    const auto& preset = list[static_cast<size_t>(juce::jlimit(0, static_cast<int>(list.size()) - 1, index))];
    return juce::String(preset.genre) + " - " + preset.name;
}

void Bass808Processor::applyUserPreset(const BassPresetFile& preset)
{
    if (preset.sample != nullptr)
        setSampleDirect(preset.sample);
    writeSettings(preset.settings);
    userPresetName = preset.name;
}

void Bass808Processor::newDrawPreset()
{
    BassSettings s;
    s.drawWaveEnabled = true;
    s.drawnWave = std::make_shared<const BassDrawWave>(BassDrawWave::Points {});
    s.punchSemitones = 0.0f;
    s.drive = 1.0f;
    s.bias = 0.0f;
    s.saturation = 0.0f;
    s.clickDb = -60.0f;
    s.toneHz = 16000.0f;
    s.attackMs = 5.0f;
    s.sustain = 1.0f;
    s.releaseMs = 70.0f;
    s.mode = BassSettings::Retrigger;
    s.gainDb = -6.0f;
    clearSample();
    writeSettings(s);
    userPresetName = "Untitled";
}

std::shared_ptr<const BassDrawWave> Bass808Processor::getDrawWave() const
{
    return std::atomic_load(&drawnWave);
}

void Bass808Processor::setDrawWave(std::shared_ptr<const BassDrawWave> wave)
{
    // Keep replaced tables on the publisher side until neither audio voice can still use them.
    const std::lock_guard<std::mutex> lock(drawPublisherMutex);
    if (getDrawWave() == wave)
        return;
    if (auto old = std::atomic_exchange(&drawnWave, std::move(wave));
        old != nullptr && std::find(retiredDrawWaves.begin(), retiredDrawWaves.end(), old) == retiredDrawWaves.end())
        retiredDrawWaves.push_back(std::move(old));
    retiredDrawWaves.erase(std::remove_if(retiredDrawWaves.begin(), retiredDrawWaves.end(),
                                        [](const auto& item) { return item.use_count() == 1; }), retiredDrawWaves.end());
}

Bass808Processor::ReferenceView Bass808Processor::waveformReference() const
{
    const std::lock_guard<std::mutex> lock(referenceMutex);
    return referenceView;
}

double Bass808Processor::snapTime(double seconds, double end) const noexcept
{
    const int division = snapDivision();
    const double step = division > 0 ? 240.0 / (hostTempo() * division) : 0.0;
    if (step > 0.0)
        seconds = std::round(seconds / step) * step;
    return juce::jlimit(0.0, std::max(0.0, end), seconds);
}

void Bass808Processor::setWaveformReference(std::shared_ptr<const Bass808Reference> audio)
{
    const std::lock_guard<std::mutex> lock(referenceMutex);
    referenceView = {};
    if (audio == nullptr)
        return;
    const double period = 1.0 / BassEngine::noteHz(lastNote(), 0.0f);
    referenceView.window = std::min(period, audio->duration());
    referenceView.audio = std::move(audio);
}

void Bass808Processor::setReferenceWindow(double start, double window)
{
    const std::lock_guard<std::mutex> lock(referenceMutex);
    if (referenceView.audio == nullptr || !std::isfinite(start) || !std::isfinite(window))
        return;
    const double end = referenceView.audio->duration();
    referenceView.window = juce::jlimit(std::min(0.001, end), end, window);
    referenceView.start = juce::jlimit(0.0, end - referenceView.window, start);
}

juce::String Bass808Processor::presetName() const
{
    if (userPresetName.isNotEmpty())
        return userPresetName;
    const auto& list = programs();
    return list[static_cast<size_t>(juce::jlimit(0, static_cast<int>(list.size()) - 1, currentPreset))].name;
}

void Bass808Processor::setSampleDirect(std::shared_ptr<const BassSample> sample)
{
    retire(engine.getSample());
    engine.setSample(std::move(sample));
}

void Bass808Processor::applyPreset(int index)
{
    userPresetName = {};
    const auto& list = programs();
    currentPreset = juce::jlimit(0, static_cast<int>(list.size()) - 1, index);
    auto settings = list[static_cast<size_t>(currentPreset)].settings;
    if (getSample() != nullptr && settings.sampleLevelDb <= -59.5f)
    {
        // a loaded sample stays where the user put it: the synth presets keep the sample layer's settings
        const auto now = currentSettings();
        settings.sampleLevelDb = now.sampleLevelDb;
        settings.sampleTune = now.sampleTune;
        settings.sampleFine = now.sampleFine;
        settings.sampleStart = now.sampleStart;
        settings.sampleEnd = now.sampleEnd;
        settings.sampleLoop = now.sampleLoop;
        settings.sampleLoopStart = now.sampleLoopStart;
        settings.sampleStretch = now.sampleStretch;
        settings.sampleLockTime = now.sampleLockTime;
        settings.sampleFollow = now.sampleFollow;
        settings.sampleReverse = now.sampleReverse;
        settings.sampleInvert = now.sampleInvert;
    }
    writeSettings(settings);
    apvts.state.setProperty("preset", currentPreset, nullptr);
}

void Bass808Processor::retire(std::shared_ptr<const BassSample> old)
{
    retired.erase(std::remove_if(retired.begin(), retired.end(), [](const auto& s) { return s.use_count() == 1; }), retired.end());
    if (old != nullptr)
        retired.push_back(std::move(old));
}

bool Bass808Processor::loadSample(const juce::File& file, juce::String& error)
{
    auto sample = BassSample::fromFile(file, error);
    if (sample == nullptr)
        return false;
    retire(engine.getSample());
    engine.setSample(std::move(sample));
    setParameter("smpStart", 0.0f);
    setParameter("smpEnd", 1.0f);
    setParameter("smpLoopStart", 0.5f);
    if (currentSettings().sampleLevelDb <= -59.5f)
        setParameter("smpLevel", 0.0f);
    return true;
}

void Bass808Processor::clearSample()
{
    retire(engine.getSample());
    engine.setSample(nullptr);
}

void Bass808Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);
    master.prepare(sampleRate);
    mono.assign(static_cast<size_t>(juce::jmax(1, samplesPerBlock)), 0.0f);
    left = right = mono;
    setLatencySamples(engine.latencySamples());
}

bool Bass808Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void Bass808Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (static_cast<int>(mono.size()) < numSamples)
    {
        mono.resize(static_cast<size_t>(numSamples)); // a host that sends more than it announced
        left.resize(mono.size());
        right.resize(mono.size());
    }
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    bool hostValid = false;
    int numerator = 4, denominator = 4;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                hostValid = std::isfinite(*bpm) && *bpm >= 20.0 && *bpm <= 400.0;
                if (hostValid)
                    hostBpm.store(*bpm, std::memory_order_relaxed);
            }
            if (const auto signature = position->getTimeSignature(); hostValid && signature && signature->numerator > 0 && signature->denominator > 0)
            {
                numerator = signature->numerator;
                denominator = signature->denominator;
            }
        }
    hostNumerator.store(numerator);
    hostDenominator.store(denominator);
    hasHostTempo.store(hostValid);
    engine.setTempo(hostTempo());
    const auto settings = currentSettings();
    engine.setSettings(settings);

    int position = 0;
    for (const auto metadata : midi)
    {
        const int at = juce::jlimit(position, numSamples, metadata.samplePosition);
        if (at > position)
            engine.render(mono.data() + position, at - position);
        position = at;
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
            lastPlayedNote.store(message.getNoteNumber());
        }
        else if (message.isNoteOff())
            engine.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }
    if (numSamples > position)
        engine.render(mono.data() + position, numSamples - position);

    // the master: mono in, stereo out (a mono output gets the middle: left + right is the mono sound)
    master.process(mono.data(), left.data(), right.data(), numSamples, settings);
    masterCorrelationNow.store(master.correlation(), std::memory_order_relaxed);
    masterGainNow.store(master.gainChangeDb(), std::memory_order_relaxed);

    float peak = 0.0f;
    int write = scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = 0.5f * (left[static_cast<size_t>(i)] + right[static_cast<size_t>(i)]);
        mono[static_cast<size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
        scope[static_cast<size_t>(write)] = v;
        write = (write + 1) & (kScopeSize - 1);
    }
    scopeWrite.store(write, std::memory_order_release);
    outputPeak.store(std::max(peak, outputPeak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom(0, 0, left.data(), numSamples);
        buffer.copyFrom(1, 0, right.data(), numSamples);
    }
    else if (buffer.getNumChannels() == 1)
        buffer.copyFrom(0, 0, mono.data(), numSamples);
    if (buffer.getNumChannels() > 0)
        preview.mix(buffer, getSampleRate());
    midi.clear();
}

void Bass808Processor::copyScope(float* destination, int count) const
{
    count = juce::jlimit(0, kScopeSize, count);
    const int end = scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        destination[i] = scope[static_cast<size_t>((end - count + i + kScopeSize) & (kScopeSize - 1))];
}

juce::AudioProcessorEditor* Bass808Processor::createEditor()
{
    return new Bass808Editor(*this);
}

void Bass808Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("manualBpm", manualBpm.load(), nullptr);
    state.setProperty("gridSnap", snapDivision(), nullptr);
    writeDrawWave(state, getDrawWave());
    state.setProperty("userPresetName", userPresetName, nullptr);
    const auto guide = waveformReference();
    if (guide.audio != nullptr)
    {
        auto child = guide.audio->toTree();
        child.setProperty("start", guide.start, nullptr);
        child.setProperty("window", guide.window, nullptr);
        state.appendChild(child, nullptr);
    }
    if (const auto sample = getSample())
    {
        juce::ValueTree child("Sample");
        child.setProperty("name", sample->name, nullptr);
        child.setProperty("path", sample->path, nullptr);
        child.setProperty("wav", sample->toWavMemory().toBase64Encoding(), nullptr);
        state.appendChild(child, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void Bass808Processor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    setManualTempo(static_cast<double>(state.getProperty("manualBpm", 120.0)));
    setSnapDivision(static_cast<int>(state.getProperty("gridSnap", 0)));
    std::shared_ptr<const BassSample> sample;
    if (auto child = state.getChildWithName("Sample"); child.isValid())
    {
        juce::MemoryBlock wav;
        if (wav.fromBase64Encoding(child.getProperty("wav").toString()))
            sample = BassSample::fromWavMemory(wav, child.getProperty("name").toString(), child.getProperty("path").toString());
        state.removeChild(child, nullptr);
    }
    const auto guide = state.getChildWithName("WaveReference");
    setWaveformReference(Bass808Reference::fromTree(guide));
    setReferenceWindow(static_cast<double>(guide.getProperty("start", 0.0)), static_cast<double>(guide.getProperty("window", 0.018)));
    state.removeChild(guide, nullptr);
    auto wave = readDrawWave(state);
    state.removeChild(state.getChildWithName("DrawWave"), nullptr);
    userPresetName = state.getProperty("userPresetName").toString();
    if (!state.getChildWithProperty("id", "oscADraw").isValid())
    {
        juce::ValueTree parameter("PARAM");
        parameter.setProperty("id", "oscADraw", nullptr);
        parameter.setProperty("value", 0.0f, nullptr);
        state.appendChild(parameter, nullptr);
    }
    // JUCE can retain the current parameter value for an absent node on state replacement.
    // Supply the OSC B defaults when opening a project saved before those parameters existed.
    const BassSettings defaults;
    for (const auto& p : floatParams())
    {
        if (p.version != 4 || state.getChildWithProperty("id", p.id).isValid())
            continue;
        juce::ValueTree parameter("PARAM");
        parameter.setProperty("id", p.id, nullptr);
        parameter.setProperty("value", defaults.*(p.field), nullptr);
        state.appendChild(parameter, nullptr);
    }
    setDrawWave(std::move(wave));
    apvts.replaceState(state);
    currentPreset = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(apvts.state.getProperty("preset", 0)));
    retire(engine.getSample());
    engine.setSample(std::move(sample));
}
} // namespace bbg

#if !HPDG_BASS808_EMBEDDED // inside HPDG (the Sub808 lane's editor) HPDG is the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bbg::Bass808Processor();
}
#endif
