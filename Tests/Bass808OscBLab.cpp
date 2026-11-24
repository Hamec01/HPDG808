#include "Bass808/Bass808Processor.h"
#include "Bass808/BassExport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace bbg;

namespace
{
constexpr double kBpm = 140.0;
constexpr double kBeat = 60.0 / kBpm;
constexpr int kBlock = 512;
constexpr int kNotes[] { 24, 29, 33, 36 };
constexpr int kRates[] { 44100, 48000, 96000 };

struct NoteEvent { double beat; int note; bool on; };
constexpr NoteEvent kPhrase[] {
    { 0.0, 24, true }, { .65, 24, false }, { 1.0, 24, true }, { 1.3, 24, false },
    { 1.5, 29, true }, { 2.25, 29, false }, { 2.75, 33, true }, { 3.25, 36, true },
    { 3.5, 33, false }, { 4.5, 31, true }, { 4.75, 36, false }, { 5.5, 31, false },
    { 6.0, 24, true }, { 7.2, 24, false },
};

juce::MidiMessage messageFor(const NoteEvent& event)
{
    return event.on ? juce::MidiMessage::noteOn(1, event.note, static_cast<juce::uint8>(127))
                    : juce::MidiMessage::noteOff(1, event.note);
}

template <size_t eventCount>
juce::AudioBuffer<float> render(const Bass808Processor& patch, int rate, double seconds, const NoteEvent (&events)[eventCount])
{
    // Every take starts with the same click noise seed, including B-off and high-rate references.
    Bass808Processor bass;
    bass.writeSettings(patch.currentSettings());
    bass.setSampleDirect(patch.getSample());
    bass.setPlayConfigDetails(0, 2, rate, kBlock);
    bass.prepareToPlay(rate, kBlock);
    juce::AudioBuffer<float> audio(2, juce::roundToInt(seconds * rate));
    juce::MidiBuffer midi;
    size_t next = 0;
    for (int offset = 0; offset < audio.getNumSamples(); offset += kBlock)
    {
        const int blockSize = juce::jmin(kBlock, audio.getNumSamples() - offset);
        while (next < eventCount)
        {
            const auto& event = events[next];
            const int position = juce::roundToInt(event.beat * kBeat * rate);
            if (position >= offset + blockSize)
                break;
            midi.addEvent(messageFor(event), position - offset);
            ++next;
        }
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, offset, blockSize);
        bass.processBlock(block, midi);
    }
    return audio;
}

juce::AudioBuffer<float> note(const Bass808Processor& bass, int rate, int key)
{
    const NoteEvent events[] { { 0.0, key, true }, { 2.8, key, false } };
    return render(bass, rate, 1.5, events);
}

bool equalAudio(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float tolerance = 0.0f)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (!(std::abs(a.getSample(c, i) - b.getSample(c, i)) <= tolerance))
                return false;
    return true;
}

bool stateAndExport(const juce::File& out, int first)
{
    Bass808Processor bass;
    const auto old = note(bass, 48000, 33);
    juce::MemoryBlock state;
    bass.getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    if (xml == nullptr)
        return false;
    auto legacy = juce::ValueTree::fromXml(*xml);
    for (const auto* id : { "oscBLevel", "oscBShape", "oscBDecay", "oscBSustain" })
        legacy.removeChild(legacy.getChildWithProperty("id", id), nullptr);
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), state);
    bass.applyPreset(first + 7);
    bass.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto restored = bass.currentSettings();
    const BassSettings defaults;
    const bool legacyDefaults = restored.oscBLevelDb == defaults.oscBLevelDb
        && restored.oscBShape == defaults.oscBShape && std::abs(restored.oscBDecayMs - defaults.oscBDecayMs) < 0.001f
        && restored.oscBSustain == defaults.oscBSustain;
    const bool legacyAudio = equalAudio(old, note(bass, 48000, 33), 1.0e-6f);
    const bool legacyOk = bass.getCurrentProgram() == 0 && legacyDefaults && legacyAudio;
    std::printf("legacy program %d, OSC B %.4f dB, shape %.5f, decay %.5f ms, sustain %.5f, audio %d\n",
                bass.getCurrentProgram(), restored.oscBLevelDb, restored.oscBShape, restored.oscBDecayMs, restored.oscBSustain, legacyAudio);

    bass.applyPreset(first + 7);
    const auto grit = note(bass, 48000, 33);
    bass.getStateInformation(state);
    bass.applyPreset(0);
    bass.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const bool stateOk = bass.getCurrentProgram() == first + 7 && equalAudio(grit, note(bass, 48000, 33));

    const auto settings = bass.currentSettings();
    const auto file = out.getChildFile("osc-b-roundtrip.hpbass");
    std::vector<float> wave(4410);
    for (size_t i = 0; i < wave.size(); ++i)
        wave[i] = static_cast<float>(0.4 * std::sin(juce::MathConstants<double>::twoPi * 55.0 * static_cast<double>(i) / 44100.0));
    const auto sample = BassSample::fromBuffer(std::move(wave), 44100.0, "Roundtrip test sine");
    if (!writeBassPreset(file, { "OSC B roundtrip", settings, sample }))
        return false;
    const auto preset = readBassPreset(file);
    if (!preset || preset->sample == nullptr)
        return false;
    bass.applyPreset(0);
    bass.applyUserPreset(*preset);
    const bool presetOk = equalAudio(grit, note(bass, 48000, 33));

    const auto exported = BassExport::render(settings, nullptr, 33, kBpm);
    const auto wav = out.getChildFile("osc-b-export.wav");
    if (!BassExport::writeWav(wav, exported))
        return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
    juce::AudioBuffer<float> readback(2, exported.getNumSamples());
    const bool exportOk = reader != nullptr && reader->numChannels == 2 && reader->bitsPerSample == 24
        && reader->sampleRate == 48000.0 && reader->lengthInSamples == exported.getNumSamples()
        && reader->read(&readback, 0, readback.getNumSamples(), 0, true, true)
        && equalAudio(exported, readback, 1.0e-6f);
    std::printf("legacy DAW state: %d | new state: %d | preset with sample: %d | WAV export: %d\n",
                legacyOk, stateOk, presetOk, exportOk);
    return legacyOk && stateOk && presetOk && exportOk;
}

bool saveMidi(const juce::File& file)
{
    constexpr short ticksPerBeat = 960;
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ticksPerBeat);
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(kBeat * 1.0e6)));
    for (const auto& event : kPhrase)
        track.addEvent(messageFor(event).withTimeStamp(event.beat * ticksPerBeat));
    midi.addTrack(track);
    juce::FileOutputStream stream(file);
    return stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk() && midi.writeTo(stream);
}
} // namespace

int runBass808OscBLab(const juce::File& out)
{
    if (out.createDirectory().failed())
        return 1;
    Bass808Processor bass;
    const int first = static_cast<int>(bassPresets().size());
    if (bass.getNumPrograms() - first != 12 || !saveMidi(out.getChildFile("phrase-140.mid")))
        return 1;
    bool ok = stateAndExport(out, first);
    for (int i = 0; i < first; ++i)
        if (bass.getProgramName(i) != juce::String(bassPresets()[static_cast<size_t>(i)].genre) + " - "
                                        + bassPresets()[static_cast<size_t>(i)].name)
            return 1;

    juce::String manifest("program\tname\n");
    float highest = 0.0f;
    const auto write = [&out, &ok](const juce::String& name, const juce::AudioBuffer<float>& audio, int rate)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer(channel);
            if (!std::all_of(samples, samples + audio.getNumSamples(), [](float x) { return std::isfinite(x); }))
            {
                std::printf("non-finite audio in %s\n", name.toRawUTF8());
                ok = false;
                return;
            }
        }
        ok = BassExport::writeWav(out.getChildFile(name + ".wav"), audio, rate) && ok;
    };
    for (int i = first; i < bass.getNumPrograms(); ++i)
    {
        bass.applyPreset(i);
        manifest += juce::String(i) + "\t" + bass.getProgramName(i) + "\n";
        const auto prefix = juce::String(i) + "-";
        float worst = 0.0f;
        for (const int rate : kRates)
        {
            for (const int key : kNotes)
            {
                const auto audio = note(bass, rate, key);
                worst = std::max(worst, audio.getMagnitude(0, audio.getNumSamples()));
                write(prefix + juce::String(rate) + "-note-" + juce::String(key), audio, rate);
            }
            const auto audio = render(bass, rate, 8.0 * kBeat, kPhrase);
            worst = std::max(worst, audio.getMagnitude(0, audio.getNumSamples()));
            write(prefix + juce::String(rate) + "-phrase", audio, rate);
        }
        if (i - first >= 6 && i - first < 9)
            for (const int key : { 33, 36 })
                write(prefix + "192000-note-" + juce::String(key), note(bass, 192000, key), 192000);

        auto settings = bass.currentSettings();
        settings.oscBLevelDb = BassSettings::kOscBOffDb;
        bass.writeSettings(settings);
        write(prefix + "48000-phrase-off", render(bass, 48000, 8.0 * kBeat, kPhrase), 48000);
        for (const int key : kNotes)
            write(prefix + "48000-note-" + juce::String(key) + "-off", note(bass, 48000, key), 48000);
        std::printf("%3d %-40s %.2f dBFS\n", i, bass.getProgramName(i).toRawUTF8(), 20.0 * std::log10(worst));
        highest = std::max(highest, worst);
    }
    for (int i = 0; i < first; ++i)
    {
        const auto& p = bassPresets()[static_cast<size_t>(i)];
        if (juce::String(p.genre) == "Trap" || juce::String(p.genre) == "Drill")
        {
            bass.applyPreset(i);
            manifest += juce::String(i) + "\t" + bass.getProgramName(i) + "\n";
            write(juce::String(i) + "-48000-phrase", render(bass, 48000, 8.0 * kBeat, kPhrase), 48000);
        }
    }
    std::printf("highest note or phrase peak %.2f dBFS\n", 20.0 * std::log10(highest));
    return out.getChildFile("programs.tsv").replaceWithText(manifest) && ok
        && highest <= juce::Decibels::decibelsToGain(-1.0f) ? 0 : 1;
}
