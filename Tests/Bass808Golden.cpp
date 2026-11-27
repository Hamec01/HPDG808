// Golden renders (Bass Architecture 2.0, P1): every factory and retired preset through the engine (one held note at
// MIDI 36 and 48) and through the whole plugin (a phrase: a note, a legato step, a retrigger; the master included).
// Each render is fingerprinted (FNV-1a over the float bits) with its RMS and peak. `--golden-write` writes the table,
// `--golden-check` renders again and compares: the Classic sound must stay bit-identical while the engine grows.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Bass808/Bass808Processor.h"
#include "Bass808/BassEngine.h"

namespace
{
struct Print
{
    std::uint64_t hash = 1469598103934665603ull;
    double sum = 0.0, peak = 0.0;
    std::int64_t count = 0;
    void add(float x)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &x, sizeof bits);
        for (int b = 0; b < 4; ++b)
        {
            hash ^= (bits >> (8 * b)) & 0xffu;
            hash *= 1099511628211ull;
        }
        sum += static_cast<double>(x) * x;
        peak = std::max(peak, static_cast<double>(std::abs(x)));
        ++count;
    }
    juce::String row(const juce::String& key) const
    {
        const double rms = count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
        return key + "\t" + juce::String::toHexString(static_cast<juce::int64>(hash)) + "\t"
             + juce::String(juce::Decibels::gainToDecibels(rms, -200.0), 3) + "\t"
             + juce::String(juce::Decibels::gainToDecibels(peak, -200.0), 3);
    }
};

juce::String safe(const char* text) { return juce::String(text).replaceCharacter('\t', ' '); }

juce::StringArray renderAll()
{
    using namespace bbg;
    juce::StringArray rows;
    const auto engineRows = [&rows](const std::vector<BassPreset>& list, const char* bank)
    {
        for (const auto& preset : list)
            for (const int note : { 36, 48 })
            {
                Print print;
                for (const float x : BassEngine::renderNote(preset.settings, nullptr, note, 1.0, 48000.0))
                    print.add(x);
                rows.add(print.row(juce::String(bank) + "|engine|" + safe(preset.genre) + "|" + safe(preset.name) + "|" + juce::String(note)));
            }
    };
    engineRows(bassPresets(), "factory");
    engineRows(retiredBassPresets(), "retired");

    // the plugin: its own programme list, 2 s phrase, 512-sample blocks
    Bass808Processor bass;
    bass.setPlayConfigDetails(0, 2, 48000.0, 512);
    bass.prepareToPlay(48000.0, 512);
    for (int program = 0; program < bass.getNumPrograms(); ++program)
    {
        bass.setCurrentProgram(program);
        bass.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        Print print;
        for (int block = 0; block < 188; ++block) // ~2 s
        {
            juce::MidiBuffer midi;
            const int start = block * 512;
            const auto at = [&](int sample, const juce::MidiMessage& m)
            {
                if (sample >= start && sample < start + 512)
                    midi.addEvent(m, sample - start);
            };
            at(0, juce::MidiMessage::noteOn(1, 36, 0.8f));
            at(24000, juce::MidiMessage::noteOn(1, 43, 0.8f));
            at(24100, juce::MidiMessage::noteOff(1, 36));
            at(48000, juce::MidiMessage::noteOff(1, 43));
            at(57600, juce::MidiMessage::noteOn(1, 48, 1.0f));
            at(86400, juce::MidiMessage::noteOff(1, 48));
            bass.processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    print.add(buffer.getSample(ch, i));
        }
        rows.add(print.row("plugin|" + juce::String(program) + "|" + bass.getProgramName(program).replaceCharacter('\t', ' ')));
    }
    return rows;
}
} // namespace

int writeGolden(const juce::File& file)
{
    const auto rows = renderAll();
    file.getParentDirectory().createDirectory();
    file.replaceWithText("key\thash\trms_db\tpeak_db\n" + rows.joinIntoString("\n") + "\n");
    std::printf("%d golden renders -> %s\n", rows.size(), file.getFullPathName().toRawUTF8());
    return 0;
}

int checkGolden(const juce::File& file)
{
    juce::StringArray want;
    want.addLines(file.loadFileAsString());
    want.removeEmptyStrings();
    if (want.size() < 2)
    {
        std::printf("FAIL: no golden table at %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }
    want.remove(0);
    const auto got = renderAll();
    int changed = 0, missing = 0;
    for (const auto& line : want)
    {
        const auto key = line.upToFirstOccurrenceOf("\t", false, false);
        int found = -1;
        for (int i = 0; i < got.size(); ++i)
            if (got[i].upToFirstOccurrenceOf("\t", false, false) == key)
            {
                found = i;
                break;
            }
        if (found < 0)
        {
            ++missing;
            std::printf("[missing] %s\n", key.toRawUTF8());
        }
        else if (got[found] != line)
        {
            ++changed;
            std::printf("[changed] %s\n   was %s\n   now %s\n", key.toRawUTF8(), line.fromFirstOccurrenceOf("\t", false, false).toRawUTF8(),
                        got[found].fromFirstOccurrenceOf("\t", false, false).toRawUTF8());
        }
    }
    std::printf("%d golden renders, %d changed, %d missing, %d new\n%s\n", want.size(), changed, missing,
                juce::jmax(0, got.size() - (want.size() - missing)), changed + missing == 0 ? "PASS" : "FAIL");
    return changed + missing == 0 ? 0 : 1;
}
