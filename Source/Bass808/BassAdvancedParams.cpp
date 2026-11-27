#include "BassAdvancedParams.h"

namespace bbg
{
namespace
{
using S = BassSettings;

AdvancedParam fl(juce::String id, juce::String name, float low, float high, float centre, float step, juce::String unit,
                 std::function<float(const S&)> get, std::function<void(S&, float)> set)
{
    AdvancedParam p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.kind = AdvancedParam::Float;
    p.low = low;
    p.high = high;
    p.centre = centre;
    p.step = step;
    p.unit = std::move(unit);
    p.get = std::move(get);
    p.set = std::move(set);
    return p;
}

AdvancedParam ch(juce::String id, juce::String name, const char* choices, std::function<float(const S&)> get,
                 std::function<void(S&, float)> set)
{
    AdvancedParam p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.kind = AdvancedParam::Choice;
    p.choices = juce::StringArray::fromTokens(choices, ",", "");
    p.low = 0.0f;
    p.high = static_cast<float>(p.choices.size() - 1);
    p.step = 1.0f;
    p.get = std::move(get);
    p.set = std::move(set);
    return p;
}

AdvancedParam bo(juce::String id, juce::String name, std::function<float(const S&)> get, std::function<void(S&, float)> set)
{
    AdvancedParam p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.kind = AdvancedParam::Bool;
    p.high = 1.0f;
    p.step = 1.0f;
    p.get = std::move(get);
    p.set = std::move(set);
    return p;
}

int asInt(float v) { return juce::roundToInt(v); }

#define ADV_F(field) [](const S& s) { return static_cast<float>(s.field); }, [](S& s, float v) { s.field = v; }
#define ADV_I(field) [](const S& s) { return static_cast<float>(s.field); }, [](S& s, float v) { s.field = asInt(v); }
#define ADV_B(field) [](const S& s) { return s.field ? 1.0f : 0.0f; }, [](S& s, float v) { s.field = v >= 0.5f; }

constexpr const char* kWaves = "Sine,Triangle,Saw,Square,Pulse";
constexpr const char* kFilters = "LP12,LP24,HP12,BP";
constexpr const char* kSync = "Free,1/1,1/2,1/4,1/8,1/16,1/4T,1/8T,1/16T";
constexpr const char* kLfoShapes = "Sine,Triangle,Saw Up,Saw Down,Square,S&H,Smooth Random";
constexpr const char* kSources = "None,LFO 1,LFO 2,LFO 3,LFO 4,Filter Env,Mod Env,Amp Env,Velocity,Note";
constexpr const char* kDests = "None,Pitch,OSC A Pitch,OSC B Pitch,OSC C Pitch,OSC A Level,OSC B Level,OSC C Level,"
                               "OSC A Shape,OSC B Shape,OSC C Shape,OSC A PM,OSC B PM,OSC C PM,Cutoff A,Resonance A,"
                               "Cutoff B,Drive,Detune,Noise,Sub,Width,Amp";

std::vector<AdvancedParam> makeParams()
{
    std::vector<AdvancedParam> list;
    list.push_back(ch("advEngine", "Engine", "Classic,Advanced", ADV_I(engine)));
    for (int o = 0; o < 3; ++o)
    {
        const juce::String x = juce::String::charToString(static_cast<juce::juce_wchar>('A' + o));
        const juce::String id = "advOsc" + x, name = "OSC " + x + " ";
        const auto osc = [o](S& s) -> S::AdvOsc& { return s.advOsc[static_cast<size_t>(o)]; };
        const auto cosc = [o](const S& s) -> const S::AdvOsc& { return s.advOsc[static_cast<size_t>(o)]; };
        list.push_back(bo(id + "On", name + "On", [cosc](const S& s) { return cosc(s).on ? 1.0f : 0.0f; },
                          [osc](S& s, float v) { osc(s).on = v >= 0.5f; }));
        list.push_back(ch(id + "Wave", name + "Wave", kWaves, [cosc](const S& s) { return static_cast<float>(cosc(s).wave); },
                          [osc](S& s, float v) { osc(s).wave = asInt(v); }));
        list.push_back(fl(id + "Oct", name + "Octave", -3, 3, 0, 1, "oct", [cosc](const S& s) { return static_cast<float>(cosc(s).octave); },
                          [osc](S& s, float v) { osc(s).octave = asInt(v); }));
        list.push_back(fl(id + "Semi", name + "Semitones", -24, 24, 0, 1, "st", [cosc](const S& s) { return cosc(s).semitones; },
                          [osc](S& s, float v) { osc(s).semitones = v; }));
        list.push_back(fl(id + "Fine", name + "Fine", -100, 100, 0, 0.1f, "ct", [cosc](const S& s) { return cosc(s).fine; },
                          [osc](S& s, float v) { osc(s).fine = v; }));
        list.push_back(fl(id + "Level", name + "Level", -60, 6, 0, 0.1f, "dB", [cosc](const S& s) { return cosc(s).levelDb; },
                          [osc](S& s, float v) { osc(s).levelDb = v; }));
        list.push_back(fl(id + "Pan", name + "Pan", -1, 1, 0, 0.01f, "", [cosc](const S& s) { return cosc(s).pan; },
                          [osc](S& s, float v) { osc(s).pan = v; }));
        list.push_back(fl(id + "Unison", name + "Unison", 1, 7, 0, 1, "", [cosc](const S& s) { return static_cast<float>(cosc(s).unison); },
                          [osc](S& s, float v) { osc(s).unison = asInt(v); }));
        list.push_back(fl(id + "Detune", name + "Detune", 0, 1, 0, 0.001f, "", [cosc](const S& s) { return cosc(s).detune; },
                          [osc](S& s, float v) { osc(s).detune = v; }));
        list.push_back(fl(id + "Spread", name + "Spread", 0, 1, 0, 0.01f, "", [cosc](const S& s) { return cosc(s).spread; },
                          [osc](S& s, float v) { osc(s).spread = v; }));
        list.push_back(fl(id + "Shape", name + "Shape", 0, 1, 0, 0.001f, "", [cosc](const S& s) { return cosc(s).shape; },
                          [osc](S& s, float v) { osc(s).shape = v; }));
        list.push_back(fl(id + "Phase", name + "Phase", 0, 1, 0, 0.001f, "", [cosc](const S& s) { return cosc(s).phase; },
                          [osc](S& s, float v) { osc(s).phase = v; }));
        list.push_back(bo(id + "RandPhase", name + "Random Phase", [cosc](const S& s) { return cosc(s).randomPhase ? 1.0f : 0.0f; },
                          [osc](S& s, float v) { osc(s).randomPhase = v >= 0.5f; }));
        // PM sources: only a higher-index oscillator (acyclic); index 0 = none
        list.push_back(ch(id + "PmFrom", name + "PM From", "None,OSC B,OSC C",
                          [cosc](const S& s) { return static_cast<float>(cosc(s).pmFrom < 0 ? 0 : cosc(s).pmFrom); },
                          [osc](S& s, float v) { const int i = asInt(v); osc(s).pmFrom = i <= 0 ? -1 : i; }));
        list.push_back(fl(id + "Pm", name + "PM", 0, 1, 0, 0.001f, "", [cosc](const S& s) { return cosc(s).pmAmount; },
                          [osc](S& s, float v) { osc(s).pmAmount = v; }));
        list.push_back(bo(id + "Audible", name + "Audible", [cosc](const S& s) { return cosc(s).audible ? 1.0f : 0.0f; },
                          [osc](S& s, float v) { osc(s).audible = v >= 0.5f; }));
    }
    list.push_back(bo("advSubOn", "Sub On", ADV_B(advSubOn)));
    list.push_back(ch("advSubWave", "Sub Wave", "Sine,Triangle", ADV_I(advSubWave)));
    list.push_back(fl("advSubOct", "Sub Octave", -2, 0, 0, 1, "oct", ADV_I(advSubOctave)));
    list.push_back(fl("advSubLevel", "Sub Level", -60, 6, 0, 0.1f, "dB", ADV_F(advSubLevelDb)));
    list.push_back(fl("advSubWarm", "Sub Warm", 0, 1, 0, 0.01f, "", ADV_F(advSubWarm)));
    list.push_back(fl("advNoise", "Noise", -60, 0, 0, 0.1f, "dB", ADV_F(advNoiseDb)));

    list.push_back(ch("advFltAType", "Filter A Type", kFilters, ADV_I(advFilterAType)));
    list.push_back(fl("advCutA", "Cutoff A", 20, 20000, 800, 0, "Hz", ADV_F(advCutoffA)));
    list.push_back(fl("advResA", "Resonance A", 0, 1, 0, 0.001f, "", ADV_F(advResoA)));
    list.push_back(fl("advKeyA", "Key A", 0, 1, 0, 0.01f, "", ADV_F(advKeyA)));
    list.push_back(fl("advEnvA", "Env A", -6, 6, 0, 0.01f, "oct", ADV_F(advEnvA)));
    list.push_back(fl("advDrive", "Drive", 0, 36, 0, 0.1f, "dB", ADV_F(advDriveDb)));
    list.push_back(bo("advFltBOn", "Filter B On", ADV_B(advFilterBOn)));
    list.push_back(ch("advFltBType", "Filter B Type", kFilters, ADV_I(advFilterBType)));
    list.push_back(fl("advCutB", "Cutoff B", 20, 20000, 800, 0, "Hz", ADV_F(advCutoffB)));
    list.push_back(fl("advResB", "Resonance B", 0, 1, 0, 0.001f, "", ADV_F(advResoB)));
    list.push_back(bo("advFltPar", "Filters Parallel", ADV_B(advFiltersParallel)));

    const auto envParams = [&list](const juce::String& id, const juce::String& name, S::AdvEnv S::*env)
    {
        list.push_back(fl(id + "A", name + " Attack", 0.1f, 5000, 50, 0, "ms", [env](const S& s) { return (s.*env).attackMs; },
                          [env](S& s, float v) { (s.*env).attackMs = v; }));
        list.push_back(fl(id + "D", name + " Decay", 1, 10000, 400, 0, "ms", [env](const S& s) { return (s.*env).decayMs; },
                          [env](S& s, float v) { (s.*env).decayMs = v; }));
        list.push_back(fl(id + "S", name + " Sustain", 0, 1, 0, 0.001f, "", [env](const S& s) { return (s.*env).sustain; },
                          [env](S& s, float v) { (s.*env).sustain = v; }));
        list.push_back(fl(id + "R", name + " Release", 1, 10000, 300, 0, "ms", [env](const S& s) { return (s.*env).releaseMs; },
                          [env](S& s, float v) { (s.*env).releaseMs = v; }));
    };
    envParams("advAmp", "Amp", &S::advAmpEnv);
    envParams("advFenv", "Filter Env", &S::advFilterEnv);
    envParams("advMenv", "Mod Env", &S::advModEnv);

    for (int l = 0; l < 4; ++l)
    {
        const juce::String id = "advLfo" + juce::String(l + 1), name = "LFO " + juce::String(l + 1) + " ";
        list.push_back(ch(id + "Shape", name + "Shape", kLfoShapes,
                          [l](const S& s) { return static_cast<float>(s.advLfo[static_cast<size_t>(l)].shape); },
                          [l](S& s, float v) { s.advLfo[static_cast<size_t>(l)].shape = asInt(v); }));
        list.push_back(fl(id + "Rate", name + "Rate", 0.02f, 40, 2, 0, "Hz",
                          [l](const S& s) { return s.advLfo[static_cast<size_t>(l)].rateHz; },
                          [l](S& s, float v) { s.advLfo[static_cast<size_t>(l)].rateHz = v; }));
        list.push_back(ch(id + "Sync", name + "Sync", kSync,
                          [l](const S& s) { return static_cast<float>(s.advLfo[static_cast<size_t>(l)].sync); },
                          [l](S& s, float v) { s.advLfo[static_cast<size_t>(l)].sync = asInt(v); }));
        list.push_back(bo(id + "Retrig", name + "Retrigger",
                          [l](const S& s) { return s.advLfo[static_cast<size_t>(l)].retrigger ? 1.0f : 0.0f; },
                          [l](S& s, float v) { s.advLfo[static_cast<size_t>(l)].retrigger = v >= 0.5f; }));
    }
    for (int m = 0; m < 8; ++m)
    {
        const juce::String id = "advMod" + juce::String(m + 1), name = "Mod " + juce::String(m + 1) + " ";
        list.push_back(ch(id + "Src", name + "Source", kSources,
                          [m](const S& s) { return static_cast<float>(s.advMod[static_cast<size_t>(m)].source); },
                          [m](S& s, float v) { s.advMod[static_cast<size_t>(m)].source = asInt(v); }));
        list.push_back(ch(id + "Dst", name + "Destination", kDests,
                          [m](const S& s) { return static_cast<float>(s.advMod[static_cast<size_t>(m)].dest); },
                          [m](S& s, float v) { s.advMod[static_cast<size_t>(m)].dest = asInt(v); }));
        list.push_back(fl(id + "Amt", name + "Amount", -1, 1, 0, 0.001f, "",
                          [m](const S& s) { return s.advMod[static_cast<size_t>(m)].amount; },
                          [m](S& s, float v) { s.advMod[static_cast<size_t>(m)].amount = v; }));
    }
    list.push_back(ch("advFxDrvModel", "FX Drive Model", "Off,Classic,Tape,Tube,Iron,Germanium,Vinyl", ADV_I(advFxDriveModel)));
    list.push_back(fl("advFxDrv", "FX Drive", 0, 36, 0, 0.1f, "dB", ADV_F(advFxDriveDb)));
    list.push_back(fl("advFxDrvMix", "FX Drive Mix", 0, 1, 0, 0.01f, "", ADV_F(advFxDriveMix)));
    list.push_back(fl("advMb", "Multiband", 0, 1, 0, 0.01f, "", ADV_F(advMultiband)));
    list.push_back(fl("advMbTime", "Multiband Time", 2, 300, 30, 0, "ms", ADV_F(advMultibandMs)));
    list.push_back(fl("advChoMix", "Chorus Mix", 0, 1, 0, 0.01f, "", ADV_F(advChorusMix)));
    list.push_back(fl("advChoRate", "Chorus Rate", 0.05f, 8, 0.6f, 0, "Hz", ADV_F(advChorusRateHz)));
    list.push_back(fl("advChoDepth", "Chorus Depth", 0, 1, 0, 0.01f, "", ADV_F(advChorusDepth)));
    list.push_back(fl("advWidth", "Mid Width", 0, 2, 0, 0.01f, "", ADV_F(advWidth)));
    list.push_back(fl("advMonoHz", "Mono Below", 0, 600, 0, 1, "Hz", ADV_F(advMonoHz)));
    list.push_back(fl("advMidLevel", "Mid Level", -36, 12, 0, 0.1f, "dB", ADV_F(advMidDb)));
    return list;
}
#undef ADV_F
#undef ADV_I
#undef ADV_B
} // namespace

const std::vector<AdvancedParam>& advancedParams()
{
    static const auto list = makeParams();
    return list;
}

const AdvancedParam* findAdvancedParam(const juce::String& id)
{
    for (const auto& p : advancedParams())
        if (p.id == id)
            return &p;
    return nullptr;
}

void writeAdvancedSettings(juce::ValueTree& tree, const BassSettings& s)
{
    if (s.engine != BassSettings::Advanced)
        return;
    for (const auto& p : advancedParams())
        tree.setProperty(juce::Identifier(p.id), p.get(s), nullptr);
}

void readAdvancedSettings(const juce::ValueTree& tree, BassSettings& s)
{
    const BassSettings defaults;
    for (const auto& p : advancedParams())
    {
        const juce::Identifier key(p.id);
        const float value = tree.hasProperty(key) ? static_cast<float>(tree.getProperty(key)) : p.get(defaults);
        p.set(s, juce::jlimit(p.low, p.high, value));
    }
}
} // namespace bbg
