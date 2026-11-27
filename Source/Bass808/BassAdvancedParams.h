#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "Bass808/BassSettings.h"

namespace bbg
{
// The Advanced engine's controls (BassSettings::engine and every adv field) as one table: the plugin's parameters
// (APVTS, ParameterID version 7), the settings' ValueTree (BassSettingsIO) and the editor read it. Ids are stable.
struct AdvancedParam
{
    enum Kind { Float, Choice, Bool };
    juce::String id, name;
    Kind kind = Float;
    float low = 0.0f, high = 1.0f, centre = 0.0f, step = 0.0f; // centre 0: linear; Choice: 0 .. choices - 1
    juce::String unit;
    juce::StringArray choices;
    std::function<float(const BassSettings&)> get;
    std::function<void(BassSettings&, float)> set;
};

const std::vector<AdvancedParam>& advancedParams();
const AdvancedParam* findAdvancedParam(const juce::String& id);

// Only an Advanced sound writes its fields ("adv" children of the settings tree), so a Classic tree stays as it was.
void writeAdvancedSettings(juce::ValueTree& settingsTree, const BassSettings&);
void readAdvancedSettings(const juce::ValueTree& settingsTree, BassSettings&);
} // namespace bbg
