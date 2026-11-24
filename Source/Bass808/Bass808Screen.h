#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Bass808/Bass808Processor.h"
#include "Bass808/BassDrawWave.h"

namespace bbg
{
// HPDG 808's screen, one view per tab:
//   SYNTH    the whole note as it sounds now (rendered from the knobs) and its waveform up close; the live
//            output over it while a note plays
//   SAMPLE   the loaded sample with its start / loop / end markers (drag them)
//   MIX      the synth layer, the sample layer and the sum over the first 0.6 s; drag the handoff point
//   FX       the filter's response with the note's harmonics under it; the saturator's curve and harmonics
//   MASTER   the master's response (sub filter, low band), what stays mono and what is spread; the dynamics'
//            curve with the live gain and the left / right correlation
class Bass808Screen final : public juce::Component, private juce::Timer
{
public:
    enum Mode { Synth = 0, Sample, Mix, Effects, Master };

    explicit Bass808Screen(Bass808Processor&);
    void setMode(Mode newMode);
    std::function<void()> onNewPreset;
    std::function<void()> onLoadReference;
    bool usesSynthTimeline() const noexcept { return drawAttachment != nullptr && mode == Synth; }
    void startPreview();
    void stopPreview();
    bool loadReference(const juce::File&);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void renderPreviews();
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> timelineArea() const;
    juce::Rectangle<float> referenceArea() const;
    juce::Rectangle<float> drawingArea() const;

    void paintSynth(juce::Graphics&, juce::Rectangle<float>);
    void paintDrawWave(juce::Graphics&, juce::Rectangle<float>);
    void paintTimeline(juce::Graphics&);
    void paintReference(juce::Graphics&);
    void paintBeatGrid(juce::Graphics&, juce::Rectangle<float>, double duration);
    void autoDraw();
    void selectSequenceFrame();
    bool editingSequence() const;
    Bass808Processor::ReferenceView drawingReference() const;
    void refreshPreview();
    void syncReference();
    void scrubTo(float x);
    void moveReferenceTo(float x);
    void paintSample(juce::Graphics&, juce::Rectangle<float>);
    void paintMix(juce::Graphics&, juce::Rectangle<float>);
    void paintEffects(juce::Graphics&, juce::Rectangle<float>);
    void paintMaster(juce::Graphics&, juce::Rectangle<float>);

    // sample markers: 0 start, 1 loop, 2 end
    float markerValue(int marker) const;
    const char* markerId(int marker) const;
    int markerAt(juce::Point<float>) const;
    void setParameter(const char* id, float value);
    bool drawingEnabled() const;
    void syncDrawWave();
    void rememberDrawWave();
    void publishDrawWave();
    void drawTo(juce::Point<float>);

    Bass808Processor& bass;
    Mode mode = Synth;
    BassSettings settings;
    std::vector<float> snapshot;
    std::shared_ptr<const BassSample> renderedSample;
    int renderedNote = -1;
    int settleTicks = -1;

    static constexpr double kRate = 32000.0, kSeconds = 1.6, kMixSeconds = 0.6;
    std::vector<float> full, synthLayer, sampleLayer;
    std::vector<float> live;
    bool liveActive = false;

    int dragging = -1, hovering = -1;
    const char* dragId = nullptr;

    juce::TextButton drawButton { "DRAW" }, undoButton { "UNDO" }, clearButton { "CLEAR" }, newPresetButton { "NEW PRESET" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> drawAttachment;
    BassDrawWave::Points drawPoints {};
    std::shared_ptr<const BassDrawWave> visibleDrawWave;
    struct DrawUndo { std::shared_ptr<const BassDrawWave> wave; Bass808Processor::ReferenceView reference; bool enabled;
        float attack, hold, decay, sustain, swell, punch, tune; int note; };
    std::vector<DrawUndo> drawUndo;
    int strokePoint = -1;
    float strokeValue = 0.0f;
    bool drawPending = false;

    juce::TextButton referenceButton { "LOAD REF" }, clearReferenceButton { "X" };
    juce::Slider referenceWindow;
    Bass808Processor::ReferenceView reference;
    std::vector<float> previewOverview, referenceOverview;
    std::shared_ptr<const Bass808Preview::Clip> previewClip;
    double previewCursor = 0.0;
    bool previewPlaying = false, previewDirty = true, scrubbing = false, movingReference = false;
    int previewWait = 0;
    juce::TextButton autoButton { "AUTO DRAW" };
    juce::Label bpmValue, tempoSource;
    juce::ComboBox snapBox;
    double shownTempo = -1.0;
    juce::String autoStatus;
    double drawViewStart = 0.0, drawViewWidth = 1.0;
    juce::ComboBox autoMode, frameSelector;
    size_t selectedFrame = 0;
};
} // namespace bbg
