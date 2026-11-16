#pragma once

#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Bass808/BassSample.h"
#include "Bass808/BassSettings.h"

namespace bbg
{
// HPDG 808's finished one-shot: one note of the current sound through the engine and the master, as a
// stereo sample for any sampler or DAW track (EXPORT WAV, and the DRAG handle that drops it on a track).
//
// The note: a sound without Sustain plays until its envelope ends; one with Sustain is held for
// kHeldSeconds, then released. The tail is cut where both channels stay under -90 dBFS, with a 5 ms fade.
struct BassExport
{
    static constexpr double kRate = 48000.0;
    static constexpr double kHeldSeconds = 2.0;
    static constexpr double kMaxSeconds = 10.0;

    static juce::AudioBuffer<float> render(const BassSettings&, std::shared_ptr<const BassSample>, int note, double bpm,
                                           double sampleRate = kRate);
    // 24-bit stereo WAV.
    static bool writeWav(const juce::File&, const juce::AudioBuffer<float>&, double sampleRate = kRate);
    // "HPDG808 <preset> <note>.wav", made safe for a file name.
    static juce::String fileName(const juce::String& presetName, int note);
    // Documents/HamloProd/Bass808 Exports: where the DRAG handle writes its file before the drop.
    static juce::File exportFolder();
};
} // namespace bbg
