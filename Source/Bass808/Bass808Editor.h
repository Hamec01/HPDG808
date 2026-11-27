#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "Bass808Processor.h"
#include "Bass808Screen.h"
#include "Bass808Widgets.h"

namespace bbg
{
class BassPresetBrowser;
// HPDG 808's window, in HPDG's Dark skin: preset / mode / play keys on top, the tabs (SYNTH, SAMPLE,
// MIX, FILTER · FX and, in the plugin, MASTER) that switch the screen and the controls under it. Drop an audio file anywhere on the
// window to load it into the sample layer.
class Bass808Editor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr int defaultWidth = 1000, defaultHeight = 682;
    explicit Bass808Editor(Bass808Processor& processor);
    ~Bass808Editor() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // 0 SYNTH, 1 SAMPLE, 2 MIX, 3 FILTER / FX, 4 MASTER, 5 ADVANCED
    void showTab(int index);
    void stopAudition();

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    class PlayButton;
    class DragButton;
    class SectionStrip;

    void timerCallback() override;
    void selectTab(int index);
    void stepPreset(int delta);
    void chooseSample();
    void loadSample(const juce::File& file);
    void chooseReference();
    void loadReference(const juce::File& file);
    void updatePresetName();
    void savePreset();
    void writePreset(const juce::String& name);
    void endNaming();
    // The current sound, one note (the last played), as a finished stereo WAV (BassExport).
    juce::File renderToFile(const juce::File& folder);
    void exportWav();

    Bass808Processor& bass;
    b808::Look look;
    juce::TextButton presetButton;
    juce::TextButton previousPreset { "<" }, nextPreset { ">" }, saveButton { "SAVE" };
    juce::TextEditor nameEditor;
    bool naming = false;
    std::unique_ptr<b808::ParamCombo> mode;
    std::unique_ptr<PlayButton> play;
    juce::TextButton exportButton { "EXPORT WAV" };
    // Trio: one instance plays HPDG's three DnB Dancefloor bass lanes (MIDI channel 1 this sound, 2 a Mid, 3 a Stab preset)
    juce::TextButton trioButton { "TRIO" };
    void showTrioMenu();
    juce::String trioText() const;
    std::unique_ptr<DragButton> dragButton;
    std::vector<std::unique_ptr<b808::ParamKnob>> headerKnobs;
    std::array<juce::TextButton, 6> tabs;
    int tabCount = 6; // HPDG's Sub808 lane plays the master too (stereo since 2026-11-16); 6th: ADVANCED (Bass 2.0)
    Bass808Screen screen;
    std::array<std::unique_ptr<b808::Page>, 5> pages;
    std::unique_ptr<juce::Component> advancedPanel; // the ADVANCED tab: the Advanced engine's sub-pages
    std::unique_ptr<SectionStrip> sectionStrip;
    std::unique_ptr<BassPresetBrowser> presetBrowser;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    int messageTicks = 0;
    bool dragOver = false;
};
} // namespace bbg
