#include "BassExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "Bass808/BassEngine.h"
#include "Bass808/BassMaster.h"

namespace bbg
{
juce::AudioBuffer<float> BassExport::render(const BassSettings& settings, std::shared_ptr<const BassSample> sample, int note, double bpm,
                                            double sampleRate)
{
    constexpr int block = 512;
    BassEngine engine;
    engine.prepare(sampleRate, block);
    engine.setSettings(settings);
    engine.setSample(std::move(sample));
    engine.setTempo(bpm);
    BassMaster master;
    master.prepare(sampleRate);

    const int total = static_cast<int>(kMaxSeconds * sampleRate);
    const int releaseAt = settings.sustain > 0.001f ? static_cast<int>(kHeldSeconds * sampleRate) : -1;
    const int tailAfterEnd = static_cast<int>(0.3 * sampleRate); // the master's width and harmonics ring a little
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    std::vector<float> mono(block);
    engine.noteOn(note, 1.0f);
    int done = 0, quietSince = -1;
    while (done < total)
    {
        int n = std::min(block, total - done);
        if (releaseAt > done && releaseAt < done + n)
            n = releaseAt - done; // the note off on its sample
        engine.render(mono.data(), n);
        master.process(mono.data(), out.getWritePointer(0, done), out.getWritePointer(1, done), n, settings);
        done += n;
        if (done == releaseAt)
            engine.noteOff(note);
        if (!engine.isActive())
        {
            if (quietSince < 0)
                quietSince = done;
            else if (done - quietSince >= tailAfterEnd)
                break;
        }
    }

    // the length: up to the last sample over -90 dBFS on either channel, then a 5 ms fade
    const float floor = std::pow(10.0f, -90.0f / 20.0f);
    int length = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* x = out.getReadPointer(ch);
        for (int i = done - 1; i >= length; --i)
            if (std::abs(x[i]) > floor)
            {
                length = i + 1;
                break;
            }
    }
    const int fade = std::min(length, static_cast<int>(0.005 * sampleRate));
    for (int ch = 0; ch < 2; ++ch)
        out.applyGainRamp(ch, length - fade, fade, 1.0f, 0.0f);
    out.setSize(2, std::max(1, length), true, true, false);
    return out;
}

bool BassExport::writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(file);
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(temporary.getFile());
        if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
            return false;
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                         .withSampleRate(sampleRate)
                                                         .withNumChannels(buffer.getNumChannels())
                                                         .withBitsPerSample(24));
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}

juce::String BassExport::fileName(const juce::String& presetName, int note)
{
    const auto noteName = juce::MidiMessage::getMidiNoteName(note, true, true, 4).replace("#", "s");
    return juce::File::createLegalFileName("HPDG808 " + presetName.trim() + " " + noteName) + ".wav";
}

juce::File BassExport::exportFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HamloProd").getChildFile("Bass808 Exports");
}
} // namespace bbg
