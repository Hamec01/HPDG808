#pragma once

#include "BassSettings.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace bbg
{
// Section ownership is shared by the UI and processor. Loading one of these never writes another
// section's parameters, the loaded sample, global pitch, play mode or output gain.
inline juce::StringArray bassSectionParameterIds(int section)
{
    switch (section)
    {
        case 0: return { "wave", "shape", "subOct", "sub", "punch", "punchTime", "drive", "even", "saturation",
                         "tone", "toneFall", "click", "boost", "attack", "swell", "swellTime", "hold", "decay",
                         "sustain", "release", "oscADraw" };
        case 1: return { "smpLevel", "smpTune", "smpFine", "smpStretch", "smpStart", "smpEnd", "smpLoopStart",
                         "smpLoop", "smpFollow", "smpLock", "smpReverse", "smpInvert" };
        case 2: return { "synthLevel", "smpLevel", "handoff", "smpInvert", "oscBLevel", "oscBShape", "oscBDecay",
                         "oscBSustain", "sub", "phantom" };
        case 3: return { "fltType", "cutoff", "reso", "fltKey", "fltEnv", "fltDecay", "vowel", "lfoRate",
                         "lfoSync", "lfoDepth", "lfoRetrig", "satModel", "satDrive", "satHarm", "satEvenOdd", "satMix", "phantom",
                         "lowCut" };
        case 4: return { "mSub", "mSubHz", "mFreq", "mWidth", "mLowGain", "mHarmType", "mHarm", "mHarmSide",
                         "mDampen", "mThresh", "mDyn", "mRelease", "mLimit" };
        default: return {};
    }
}

inline const char* bassSectionName(int section)
{
    static const char* names[] { "Synth", "Sample", "Mix", "Filter FX", "Master" };
    return names[juce::jlimit(0, 4, section)];
}

struct BassSectionPreset
{
    juce::String name;
    BassSettings settings;
};

inline const std::vector<BassSectionPreset>& bassSectionPresets(int section)
{
    static const auto banks = []
    {
        std::array<std::vector<BassSectionPreset>, 5> result;
        const auto add = [&](int tab, const char* name, auto edit)
        {
            BassSettings s;
            edit(s);
            result[static_cast<size_t>(tab)].push_back({ name, s });
        };
        add(0, "Clean Sub", [](auto& s) { s.drive = 1; s.saturation = 0; s.bias = 0; s.punchSemitones = 0; s.clickDb = -60; s.toneHz = 16000; s.decaySeconds = .6f; });
        add(0, "Classic 808", [](auto&) {});
        add(0, "Short Knock", [](auto& s) { s.punchSemitones = 28; s.punchMs = 12; s.holdSeconds = 0; s.decaySeconds = .12f; s.releaseMs = 25; s.toneHz = 1400; s.drive = 3; s.clickDb = -42; });
        add(0, "Long Tail", [](auto& s) { s.punchSemitones = 8; s.holdSeconds = .2f; s.decaySeconds = 1.2f; s.releaseMs = 160; s.drive = 2; s.saturation = .3f; });
        add(0, "Warm Triangle", [](auto& s) { s.wave = BassSettings::Triangle; s.shape = .2f; s.punchSemitones = 3; s.drive = 2; s.saturation = .3f; s.bias = .12f; s.toneHz = 700; s.clickDb = -60; });
        add(0, "Soft Bloom", [](auto& s) { s.attackMs = 15; s.swellDb = 14; s.swellMs = 100; s.punchSemitones = 0; s.decaySeconds = .8f; s.sustain = .4f; s.releaseMs = 180; s.saturation = .15f; s.clickDb = -60; });
        add(0, "Reese Body", [](auto& s) { s.wave = BassSettings::Reese; s.shape = .3f; s.sub = .25f; s.toneHz = 1800; s.drive = 2; s.saturation = .3f; s.punchSemitones = 0; s.sustain = .6f; s.clickDb = -60; });
        add(0, "Square Bite", [](auto& s) { s.wave = BassSettings::Square; s.shape = .3f; s.toneHz = 2200; s.drive = 2.5f; s.saturation = .45f; s.decaySeconds = .25f; s.clickDb = -60; });

        const auto sampleBase = [](auto& s) { s.sampleLevelDb = 0; };
        add(1, "Natural", sampleBase);
        add(1, "Loop Sustain", [](auto& s) { s.sampleLevelDb = 0; s.sampleLoop = true; s.sampleLoopStart = .35f; });
        add(1, "Reverse", [](auto& s) { s.sampleLevelDb = 0; s.sampleReverse = true; });
        add(1, "Half Speed", [](auto& s) { s.sampleLevelDb = 0; s.sampleStretch = 2; s.sampleLockTime = true; });
        add(1, "Double Speed", [](auto& s) { s.sampleLevelDb = 0; s.sampleStretch = .5f; s.sampleLockTime = true; });
        add(1, "Fixed Pitch", [](auto& s) { s.sampleLevelDb = 0; s.sampleFollow = false; });

        add(2, "Synth Only", [](auto&) {});
        add(2, "Sample Only", [](auto& s) { s.synthLevelDb = -60; s.sampleLevelDb = 0; });
        add(2, "Balanced Layers", [](auto& s) { s.synthLevelDb = -6; s.sampleLevelDb = -6; });
        add(2, "Transient + Sub", [](auto& s) { s.sampleLevelDb = -3; s.synthLevelDb = -3; s.handoffMs = 55; s.sub = .2f; });
        add(2, "Soft Handoff", [](auto& s) { s.sampleLevelDb = -4; s.synthLevelDb = -3; s.handoffMs = 180; });
        add(2, "Harmonic Texture", [](auto& s) { s.oscBLevelDb = -15; s.oscBShape = .45f; s.oscBSustain = .35f; s.phantom = .15f; });

        const auto cleanFx = [](auto&) {};
        add(3, "Open / Clean", cleanFx);
        add(3, "Warm Tape", [](auto& s) { s.cutoffHz = 4500; s.satModel = 2; s.satDriveDb = 5; s.satMix = .35f; });
        add(3, "Dark Low Pass", [](auto& s) { s.filterType = BassSettings::LP24; s.cutoffHz = 650; s.resonance = .16f; s.filterKey = .8f; });
        add(3, "Pluck Filter", [](auto& s) { s.filterType = BassSettings::LP24; s.cutoffHz = 240; s.filterEnvOct = 3; s.filterDecayMs = 180; s.resonance = .25f; s.filterKey = .8f; });
        add(3, "Slow Movement", [](auto& s) { s.cutoffHz = 900; s.lfoSync = 2; s.lfoDepth = 1.2f; s.resonance = .2f; });
        add(3, "Vowel Talk", [](auto& s) { s.filterType = BassSettings::Vowel; s.cutoffHz = 1800; s.vowel = 1; s.lfoSync = 3; s.lfoDepth = .8f; s.resonance = .25f; });
        add(3, "Tube Weight", [](auto& s) { s.satModel = 3; s.satDriveDb = 7; s.satMix = .4f; s.satEvenOdd = -.3f; s.phantom = .12f; });
        add(3, "Iron Grit", [](auto& s) { s.satModel = 4; s.satDriveDb = 12; s.satMix = .4f; s.cutoffHz = 6000; });

        add(4, "Neutral", [](auto&) {});
        add(4, "Sub Cleanup", [](auto& s) { s.masterSub = 1; s.masterSubHz = 25; s.masterLimit = true; });
        add(4, "Tight & Controlled", [](auto& s) { s.masterSub = 1; s.masterSubHz = 28; s.masterDynamics = -.3f; s.masterThresholdDb = -15; s.masterReleaseMs = 100; s.masterLimit = true; });
        add(4, "Warm Weight", [](auto& s) { s.masterSub = 1; s.masterSubHz = 22; s.masterLowGainDb = 1.5f; s.masterHarmonics = .12f; s.masterHarmSide = 0; s.masterLimit = true; });
        add(4, "Wide Top / Mono Sub", [](auto& s) { s.masterFreqHz = 180; s.masterWidth = 115; s.masterHarmonics = .2f; s.masterHarmSide = .8f; s.masterDampenHz = 3500; s.masterLimit = true; });
        add(4, "Small Speaker", [](auto& s) { s.masterSub = 1; s.masterSubHz = 30; s.masterHarmonics = .3f; s.masterHarmSide = .15f; s.masterDampenHz = 2400; s.masterLimit = true; });
        add(4, "Punch Focus", [](auto& s) { s.masterDynamics = .2f; s.masterThresholdDb = -18; s.masterReleaseMs = 75; s.masterSub = 1; s.masterSubHz = 25; s.masterLimit = true; });
        return result;
    }();
    return banks[static_cast<size_t>(juce::jlimit(0, 4, section))];
}

inline juce::String bassPresetCategory(const BassSettings& s)
{
    if (s.sampleLevelDb > -59.5f) return "Layered";
    if (s.wave == BassSettings::Reese) return "Reese";
    if (s.wave == BassSettings::Saw || s.wave == BassSettings::Square) return "Synth Bass";
    if (s.punchSemitones < 5 && s.saturation < .3f) return "Sub";
    return "808";
}

inline juce::StringArray bassPresetCharacters(const BassSettings& s, const juce::String& name)
{
    juce::StringArray tags;
    const auto has = [&](const char* word) { return name.containsIgnoreCase(word); };
    if (has("clean") || (s.saturation < .3f && s.satDriveDb < 4 && s.oscBLevelDb < -18)) tags.add("Clean");
    if (has("warm") || (s.bias > .06f && s.toneHz < 2000) || s.satModel == 2 || s.satModel == 3) tags.add("Warm");
    if (has("dark") || juce::jmin(s.toneHz, s.cutoffHz) < 700) tags.add("Dark");
    if (has("bright") || (s.toneHz > 2500 && s.cutoffHz > 2000)) tags.add("Bright");
    if (has("punch") || s.punchSemitones > 18) tags.add("Punchy");
    if (has("grit") || s.satDriveDb >= 8 || (s.drive >= 4 && s.saturation >= .5f)) tags.add("Gritty");
    if (has("soft") || s.attackMs >= 8 || s.swellDb >= 8) tags.add("Soft");
    if (s.lfoDepth > .1f || s.wave == BassSettings::Reese) tags.add("Moving");
    if (s.decaySeconds < .25f && s.sustain < .1f) tags.add("Short");
    if (s.decaySeconds > .7f || s.sustain > .4f) tags.add("Long");
    if (has("slide") || (s.mode != BassSettings::Retrigger && s.glideMs > 20)) tags.add("Gliding");
    if (tags.isEmpty()) tags.add("Balanced");
    return tags;
}
} // namespace bbg
