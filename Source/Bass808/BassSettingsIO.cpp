#include "BassSettingsIO.h"
#include "BassDrawWave.h"
#include "BassAdvancedParams.h"

namespace bbg
{
namespace
{
const juce::Identifier kSettingsNode { "BassSettings" };
const juce::Identifier kPresetNode { "HPDG808Preset" };
const juce::Identifier kSampleNode { "Sample" };
const juce::Identifier kDrawWaveNode { "DrawWave" };

struct FloatField { const char* id; float BassSettings::*member; };
struct IntField { const char* id; int BassSettings::*member; };
struct BoolField { const char* id; bool BassSettings::*member; };

constexpr FloatField kFloats[] {
    { "shape", &BassSettings::shape }, { "sub", &BassSettings::sub }, { "synthLevelDb", &BassSettings::synthLevelDb },
    { "punchSemitones", &BassSettings::punchSemitones }, { "punchMs", &BassSettings::punchMs }, { "drive", &BassSettings::drive },
    { "bias", &BassSettings::bias }, { "saturation", &BassSettings::saturation }, { "toneHz", &BassSettings::toneHz }, { "toneFallOct", &BassSettings::toneFallOct },
    { "clickDb", &BassSettings::clickDb }, { "boostDb", &BassSettings::boostDb },
    { "oscBLevel", &BassSettings::oscBLevelDb }, { "oscBShape", &BassSettings::oscBShape },
    { "oscBDecay", &BassSettings::oscBDecayMs }, { "oscBSustain", &BassSettings::oscBSustain },
    { "attackMs", &BassSettings::attackMs }, { "swellDb", &BassSettings::swellDb }, { "swellMs", &BassSettings::swellMs }, { "holdSeconds", &BassSettings::holdSeconds }, { "decaySeconds", &BassSettings::decaySeconds },
    { "sustain", &BassSettings::sustain }, { "releaseMs", &BassSettings::releaseMs },
    { "sampleLevelDb", &BassSettings::sampleLevelDb }, { "sampleTune", &BassSettings::sampleTune }, { "sampleFine", &BassSettings::sampleFine },
    { "sampleStart", &BassSettings::sampleStart }, { "sampleEnd", &BassSettings::sampleEnd }, { "sampleLoopStart", &BassSettings::sampleLoopStart },
    { "sampleStretch", &BassSettings::sampleStretch }, { "handoffMs", &BassSettings::handoffMs },
    { "cutoffHz", &BassSettings::cutoffHz }, { "resonance", &BassSettings::resonance }, { "filterEnvOct", &BassSettings::filterEnvOct },
    { "filterDecayMs", &BassSettings::filterDecayMs }, { "filterKey", &BassSettings::filterKey }, { "vowel", &BassSettings::vowel },
    { "lfoRateHz", &BassSettings::lfoRateHz }, { "lfoDepth", &BassSettings::lfoDepth },
    { "satDriveDb", &BassSettings::satDriveDb }, { "satHarmonics", &BassSettings::satHarmonics }, { "satEvenOdd", &BassSettings::satEvenOdd },
    { "satMix", &BassSettings::satMix }, { "phantom", &BassSettings::phantom }, { "busFilter", &BassSettings::busFilter },
    { "glideMs", &BassSettings::glideMs }, { "tuneSemitones", &BassSettings::tuneSemitones }, { "gainDb", &BassSettings::gainDb },
    { "velocity", &BassSettings::velocity },
    { "masterSubHz", &BassSettings::masterSubHz }, { "masterFreqHz", &BassSettings::masterFreqHz },
    { "masterLowGainDb", &BassSettings::masterLowGainDb }, { "masterWidth", &BassSettings::masterWidth },
    { "masterHarmonics", &BassSettings::masterHarmonics }, { "masterHarmSide", &BassSettings::masterHarmSide },
    { "masterDampenHz", &BassSettings::masterDampenHz }, { "masterThresholdDb", &BassSettings::masterThresholdDb },
    { "masterReleaseMs", &BassSettings::masterReleaseMs }, { "masterDynamics", &BassSettings::masterDynamics },
};
constexpr IntField kInts[] {
    { "wave", &BassSettings::wave }, { "subOctave", &BassSettings::subOctave }, { "filterType", &BassSettings::filterType },
    { "lfoSync", &BassSettings::lfoSync }, { "satModel", &BassSettings::satModel }, { "mode", &BassSettings::mode },
    { "masterSub", &BassSettings::masterSub }, { "masterHarmType", &BassSettings::masterHarmType },
};
constexpr BoolField kBools[] {
    { "oscADraw", &BassSettings::drawWaveEnabled },
    { "sampleLoop", &BassSettings::sampleLoop }, { "sampleLockTime", &BassSettings::sampleLockTime },
    { "sampleFollow", &BassSettings::sampleFollow }, { "sampleReverse", &BassSettings::sampleReverse },
    { "sampleInvert", &BassSettings::sampleInvert }, { "lfoRetrigger", &BassSettings::lfoRetrigger },
    { "masterLimit", &BassSettings::masterLimit },
};
} // namespace

void writeDrawWave(juce::ValueTree& parent, const std::shared_ptr<const BassDrawWave>& wave)
{
    if (wave == nullptr)
        return;
    juce::MemoryOutputStream bytes;
    for (float point : wave->points())
        bytes.writeFloat(point);
    juce::ValueTree child(kDrawWaveNode);
    child.setProperty("points", bytes.getMemoryBlock().toBase64Encoding(), nullptr);
    if (!wave->frames().empty())
    {
        // Preserve all double precision in XML, including on long sequences.
        child.setProperty("duration", juce::String(wave->duration(), 17, true), nullptr);
        child.setProperty("root", juce::String(wave->rootHz(), 17, true), nullptr);
        for (const auto& frame : wave->frames())
        {
            juce::ValueTree item("Frame");
            juce::MemoryOutputStream samples;
            for (float x : frame.points) samples.writeFloat(x);
            item.setProperty("points", samples.getMemoryBlock().toBase64Encoding(), nullptr);
            item.setProperty("time", juce::String(frame.time, 17, true), nullptr);
            item.setProperty("hz", juce::String(frame.hz, 17, true), nullptr);
            item.setProperty("gain", frame.gain, nullptr);
            child.appendChild(item, nullptr);
        }
    }
    parent.appendChild(child, nullptr);
}

std::shared_ptr<const BassDrawWave> readDrawWave(const juce::ValueTree& parent)
{
    const auto child = parent.getChildWithName(kDrawWaveNode);
    if (!child.isValid())
        return nullptr;
    if (child.getNumChildren() > 0)
    {
        const double duration = child.getProperty("duration", 0.0), root = child.getProperty("root", 0.0);
        if (!std::isfinite(duration) || duration <= 0.0 || duration > 10.0 || !std::isfinite(root)
            || root < 20.0 || root > 400.0 || child.getNumChildren() > 256)
            return nullptr;
        std::vector<BassDrawWave::Frame> frames;
        for (const auto& item : child)
        {
            BassDrawWave::Frame frame;
            frame.time = item.getProperty("time", -1.0); frame.hz = item.getProperty("hz", 0.0);
            frame.gain = static_cast<float>(item.getProperty("gain", 0.0));
            juce::MemoryBlock samples;
            const auto data = item.getProperty("points").toString();
            if (!std::isfinite(frame.time) || frame.time < 0.0 || frame.time >= duration
                || (!frames.empty() && frame.time <= frames.back().time) || !std::isfinite(frame.hz)
                || frame.hz < 20.0 || frame.hz > 4000.0 || !std::isfinite(frame.gain) || frame.gain < 0.0f || frame.gain > 2.0f
                || data.length() > 4096 || !samples.fromBase64Encoding(data) || samples.getSize() != BassDrawWave::kSize * sizeof(float))
                return nullptr;
            juce::MemoryInputStream input(samples, false);
            for (float& x : frame.points)
            {
                x = input.readFloat();
                if (!std::isfinite(x) || std::abs(x) > 1.0f) return nullptr;
            }
            frames.push_back(frame);
        }
        if (frames.size() < 2) return nullptr;
        return std::make_shared<const BassDrawWave>(std::move(frames), duration, root);
    }
    const auto encoded = child.getProperty("points").toString();
    juce::MemoryBlock bytes;
    if (encoded.length() > 4096 || !bytes.fromBase64Encoding(encoded) || bytes.getSize() != BassDrawWave::kSize * sizeof(float))
        return nullptr;
    juce::MemoryInputStream stream(bytes, false);
    BassDrawWave::Points points;
    for (float& point : points)
        point = stream.readFloat();
    return std::make_shared<const BassDrawWave>(points);
}

juce::ValueTree bassSettingsToTree(const BassSettings& settings)
{
    juce::ValueTree tree(kSettingsNode);
    for (const auto& f : kFloats)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    for (const auto& f : kInts)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    for (const auto& f : kBools)
        tree.setProperty(f.id, settings.*(f.member), nullptr);
    // only when on: every sound saved before it existed keeps the same tree (saved factory copies still match)
    if (settings.lowCutHz > 0.0f)
        tree.setProperty("lowCutHz", settings.lowCutHz, nullptr);
    writeAdvancedSettings(tree, settings); // only an Advanced sound
    writeDrawWave(tree, settings.drawnWave);
    return tree;
}

BassSettings bassSettingsFromTree(const juce::ValueTree& tree)
{
    BassSettings settings;
    if (!tree.isValid())
        return settings;
    for (const auto& f : kFloats)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<float>(tree.getProperty(f.id));
    for (const auto& f : kInts)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<int>(tree.getProperty(f.id));
    for (const auto& f : kBools)
        if (tree.hasProperty(f.id))
            settings.*(f.member) = static_cast<bool>(tree.getProperty(f.id));
    if (tree.hasProperty("lowCutHz"))
        settings.lowCutHz = static_cast<float>(tree.getProperty("lowCutHz"));
    readAdvancedSettings(tree, settings);
    settings.drawnWave = readDrawWave(tree);
    settings.wave = juce::jlimit(0, BassSettings::NumWaves - 1, settings.wave);
    settings.filterType = juce::jlimit(0, BassSettings::NumFilters - 1, settings.filterType);
    settings.satModel = juce::jlimit(0, 6, settings.satModel);
    settings.mode = juce::jlimit(0, 2, settings.mode);
    settings.lfoSync = juce::jlimit(0, 8, settings.lfoSync);
    settings.subOctave = juce::jlimit(0, 1, settings.subOctave);
    settings.busFilter = juce::jlimit(-1.0f, 1.0f, settings.busFilter);
    settings.lowCutHz = juce::jlimit(0.0f, 400.0f, settings.lowCutHz);
    settings.masterSub = juce::jlimit(0, 2, settings.masterSub);
    settings.masterHarmType = juce::jlimit(0, 2, settings.masterHarmType);
    return settings;
}

juce::File bassUserPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HamloProd").getChildFile("Bass808 Presets");
}

juce::Array<juce::File> bassUserPresetFiles()
{
    auto files = bassUserPresetFolder().findChildFiles(juce::File::findFiles, false, "*.hpbass");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

bool writeBassPreset(const juce::File& file, const BassPresetFile& preset)
{
    juce::ValueTree root(kPresetNode);
    root.setProperty("name", preset.name, nullptr);
    root.setProperty("version", 1, nullptr);
    root.appendChild(bassSettingsToTree(preset.settings), nullptr);
    if (preset.sample != nullptr)
    {
        juce::ValueTree sampleNode(kSampleNode);
        sampleNode.setProperty("name", preset.sample->name, nullptr);
        sampleNode.setProperty("wav", preset.sample->toWavMemory().toBase64Encoding(), nullptr);
        root.appendChild(sampleNode, nullptr);
    }
    const auto xml = root.createXml();
    if (xml == nullptr)
        return false;
    file.getParentDirectory().createDirectory();
    return xml->writeTo(file);
}

std::optional<BassPresetFile> readBassPreset(const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return std::nullopt;
    const auto root = juce::ValueTree::fromXml(*xml);
    if (!root.hasType(kPresetNode))
        return std::nullopt;
    BassPresetFile preset;
    preset.name = root.getProperty("name", file.getFileNameWithoutExtension()).toString();
    preset.settings = bassSettingsFromTree(root.getChildWithName(kSettingsNode));
    if (const auto sampleNode = root.getChildWithName(kSampleNode); sampleNode.isValid())
    {
        juce::MemoryBlock wav;
        if (wav.fromBase64Encoding(sampleNode.getProperty("wav").toString()))
            preset.sample = BassSample::fromWavMemory(wav, sampleNode.getProperty("name").toString(), {});
    }
    return preset;
}
} // namespace bbg
