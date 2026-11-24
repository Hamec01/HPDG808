#include "Bass808Screen.h"

#include <complex>

#include "Audio/Synth808Voice.h"
#include "Bass808/Bass808Widgets.h"
#include "Bass808/BassExport.h"
#include "Bass808/BassMaster.h"
#include "Bass808/BassSaturator.h"

namespace bbg
{
namespace
{
using namespace b808;

// Scientific pitch, as the rest of HPDG: 65.4 Hz is C2.
juce::String noteName(int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, 4); }

// The waveform of `data` (from `first`, `count` samples) as min / max per pixel column.
void drawWave(juce::Graphics& g, const std::vector<float>& data, size_t first, size_t count, juce::Rectangle<float> area,
              juce::Colour fill, float scale = 1.0f)
{
    if (data.empty() || count == 0 || area.getWidth() < 2.0f)
        return;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f * scale;
    juce::Path path;
    const int columns = static_cast<int>(area.getWidth());
    std::vector<float> top(static_cast<size_t>(columns)), bottom(static_cast<size_t>(columns));
    for (int x = 0; x < columns; ++x)
    {
        const size_t a = first + count * static_cast<size_t>(x) / static_cast<size_t>(columns);
        const size_t b = std::max(a + 1, first + count * static_cast<size_t>(x + 1) / static_cast<size_t>(columns));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < data.size(); ++i)
        {
            lo = std::min(lo, data[i]);
            hi = std::max(hi, data[i]);
        }
        top[static_cast<size_t>(x)] = mid - juce::jlimit(-1.0f, 1.0f, hi) * half;
        bottom[static_cast<size_t>(x)] = mid - juce::jlimit(-1.0f, 1.0f, lo) * half;
    }
    path.startNewSubPath(area.getX(), top[0]);
    for (int x = 1; x < columns; ++x)
        path.lineTo(area.getX() + static_cast<float>(x), top[static_cast<size_t>(x)]);
    for (int x = columns - 1; x >= 0; --x)
        path.lineTo(area.getX() + static_cast<float>(x), bottom[static_cast<size_t>(x)]);
    path.closeSubPath();
    g.setColour(fill.withAlpha(0.55f));
    g.fillPath(path);
    g.setColour(fill);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

// A line through `count` samples from `first` (a few periods up close).
void drawLine(juce::Graphics& g, const float* data, int count, juce::Rectangle<float> area, juce::Colour colour, float thickness, float scale)
{
    if (count < 2)
        return;
    juce::Path path;
    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(count - 1);
        const float y = area.getCentreY() - juce::jlimit(-1.2f, 1.2f, data[i] * scale) * area.getHeight() * 0.45f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(thickness * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void grid(juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows)
{
    g.setColour(colour::text.withAlpha(0.06f));
    for (int i = 1; i < columns; ++i)
        g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(columns)), area.getY(), area.getBottom());
    for (int i = 1; i < rows; ++i)
        g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * static_cast<float>(i) / static_cast<float>(rows)), area.getX(), area.getRight());
    g.setColour(colour::text.withAlpha(0.12f));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
}

void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour c, float size = 12.0f,
           juce::Justification just = juce::Justification::topLeft)
{
    g.setColour(c);
    g.setFont(juce::FontOptions(size));
    g.drawText(text, area, just);
}

float peakOf(const std::vector<float>& data)
{
    float peak = 1.0e-6f;
    for (auto v : data)
        peak = std::max(peak, std::abs(v));
    return peak;
}

template <typename GetSample>
std::vector<float> overview(size_t length, GetSample sample)
{
    const auto columns = std::min<size_t>(1024, length);
    std::vector<float> result(2 * columns);
    for (size_t x = 0; x < columns; ++x)
    {
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = x * length / columns; i < (x + 1) * length / columns; ++i)
        {
            const float value = sample(i);
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        result[2 * x] = lo;
        result[2 * x + 1] = hi;
    }
    return result;
}

// Seconds until the level (20 ms RMS) is `dropDb` under its peak.
double timeToDrop(const std::vector<float>& data, double rate, double dropDb)
{
    const int window = static_cast<int>(0.02 * rate);
    if (static_cast<int>(data.size()) < 2 * window)
        return 0.0;
    std::vector<double> level;
    double sum = 0.0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        sum += static_cast<double>(data[i]) * data[i];
        if (i >= static_cast<size_t>(window))
            sum -= static_cast<double>(data[i - static_cast<size_t>(window)]) * data[i - static_cast<size_t>(window)];
        level.push_back(sum);
    }
    const auto peakAt = std::max_element(level.begin(), level.end());
    const double threshold = *peakAt * std::pow(10.0, -dropDb / 10.0);
    for (auto it = peakAt; it != level.end(); ++it)
        if (*it < threshold)
            return static_cast<double>(std::distance(level.begin(), it)) / rate;
    return -1.0; // longer than the preview
}
} // namespace

Bass808Screen::Bass808Screen(Bass808Processor& processor) : bass(processor)
{
    if (bass.apvts.getParameter("oscADraw") != nullptr)
    {
        drawButton.setClickingTogglesState(true);
        drawButton.setTooltip("Draw one cycle for oscillator A");
        undoButton.setTooltip("Undo the last pencil stroke or clear");
        clearButton.setTooltip("Flatten the drawn waveform");
        newPresetButton.setTooltip("Start an empty drawn-wave preset");
        drawAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(bass.apvts, "oscADraw", drawButton);
        for (auto* button : { &drawButton, &undoButton, &clearButton, &newPresetButton, &referenceButton, &clearReferenceButton, &autoButton })
            addChildComponent(*button);
        autoButton.onClick = [this] { autoDraw(); };
        autoMode.addItem("FULL", 1);
        autoMode.addItem("CYCLE", 2);
        autoMode.setSelectedId(1);
        autoMode.onChange = [this] { syncReference(); repaint(); };
        autoMode.setName("Auto Draw mode");
        autoMode.setTooltip("FULL rebuilds the whole reference. CYCLE extracts one repeating waveform.");
        addChildComponent(autoMode);
        frameSelector.setName("Sequence frame");
        frameSelector.onChange = [this] { selectSequenceFrame(); };
        addChildComponent(frameSelector);
        autoButton.setTooltip("Extract one stable cycle from the selected reference window");
        bpmValue.setEditable(false, true);
        bpmValue.setText(juce::String(bass.hostTempo(), 2), juce::dontSendNotification);
        tempoSource.setText(bass.tempoFromHost() ? "HOST" : "BPM", juce::dontSendNotification);
        bpmValue.setTooltip("Manual BPM when host tempo is unavailable");
        bpmValue.onTextChange = [this] { bass.setManualTempo(bpmValue.getText().getDoubleValue()); };
        tempoSource.setFont(juce::FontOptions(10.0f));
        bpmValue.setFont(juce::FontOptions(12.0f));
        for (auto* label : { &bpmValue, &tempoSource })
        {
            label->setColour(juce::Label::textColourId, colour::tealBright);
            addChildComponent(*label);
        }
        snapBox.addItem("Snap Off", 1);
        for (int division : { 4, 8, 16, 32 })
            snapBox.addItem("Snap 1/" + juce::String(division), division);
        snapBox.setSelectedId(bass.snapDivision() == 0 ? 1 : bass.snapDivision(), juce::dontSendNotification);
        snapBox.onChange = [this] { bass.setSnapDivision(snapBox.getSelectedId()); repaint(); };
        addChildComponent(snapBox);
        referenceButton.setTooltip("Load at most the first ten seconds as a silent drawing guide");
        clearReferenceButton.setTooltip("Remove the reference overlay");
        referenceButton.onClick = [this] { if (onLoadReference) onLoadReference(); };
        clearReferenceButton.onClick = [this]
        {
            bass.setWaveformReference(nullptr);
            syncReference();
        };
        referenceWindow.setName("Reference window");
        referenceWindow.setRange(1.0, Bass808Reference::kMaxSeconds * 1000.0, 0.1);
        referenceWindow.setSkewFactorFromMidPoint(18.0);
        referenceWindow.setSliderStyle(juce::Slider::LinearHorizontal);
        referenceWindow.setTextBoxStyle(juce::Slider::TextBoxRight, false, 61, 20);
        referenceWindow.setTextValueSuffix(" ms");
        referenceWindow.setTooltip("Length of the reference slice stretched to one drawn cycle");
        referenceWindow.onValueChange = [this]
        {
            bass.setReferenceWindow(reference.start, referenceWindow.getValue() / 1000.0);
            syncReference();
        };
        addChildComponent(referenceWindow);

        undoButton.onClick = [this]
        {
            syncDrawWave();
            if (drawUndo.empty())
                return;
            auto undo = std::move(drawUndo.back());
            drawUndo.pop_back();
            auto restoredWave = std::move(undo.wave);
            if (undo.reference.audio == reference.audio)
                bass.setReferenceWindow(undo.reference.start, undo.reference.window);
            setParameter("oscADraw", undo.enabled ? 1.0f : 0.0f);
            setParameter("attack", undo.attack);
            setParameter("hold", undo.hold);
            setParameter("decay", undo.decay);
            setParameter("sustain", undo.sustain);
            setParameter("swell", undo.swell);
            setParameter("punch", undo.punch);
            setParameter("tune", undo.tune);
            bass.setPreviewNote(undo.note);
            syncReference();
            bass.setDrawWave(std::move(restoredWave));
            auto history = std::move(drawUndo);
            syncDrawWave();
            drawUndo = std::move(history);
            settleTicks = 3;
            previewDirty = true;
            repaint();
        };
        clearButton.onClick = [this]
        {
            syncDrawWave();
            const BassDrawWave::Points flat {};
            if (drawPoints == flat)
                return;
            rememberDrawWave();
            drawPoints = flat;
            drawPending = true;
            publishDrawWave();
        };
        newPresetButton.onClick = [this]
        {
            drawViewStart = 0.0;
            drawViewWidth = 1.0;
            if (onNewPreset)
                onNewPreset();
            previewCursor = 0.0;
            syncDrawWave();
        };
    }
    live.resize(4096);
    startTimerHz(30);
}

void Bass808Screen::setMode(Mode newMode)
{
    if (newMode != Synth && usesSynthTimeline())
        stopPreview();
    const bool showTools = drawAttachment != nullptr && newMode == Synth;
    for (auto* button : { &drawButton, &undoButton, &clearButton, &newPresetButton, &referenceButton, &autoButton })
        button->setVisible(showTools);
    bpmValue.setVisible(showTools);
    tempoSource.setVisible(showTools);
    snapBox.setVisible(showTools);
    autoMode.setVisible(showTools);
    frameSelector.setVisible(showTools && visibleDrawWave != nullptr && !visibleDrawWave->frames().empty());
    mode = newMode;
    syncReference();
    settleTicks = 1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void Bass808Screen::resized()
{
    const int x = getWidth() - 12;
    newPresetButton.setBounds(x - 108, 5, 108, 22);
    clearButton.setBounds(x - 178, 5, 64, 22);
    undoButton.setBounds(x - 248, 5, 64, 22);
    drawButton.setBounds(x - 318, 5, 64, 22);
    referenceButton.setBounds(x - 433, 5, 79, 22);
    clearReferenceButton.setBounds(x - 348, 5, 24, 22);
    const auto ref = referenceArea().toNearestInt();
    referenceWindow.setBounds(ref.getRight() + 12, ref.getY(), 146, 23);
    tempoSource.setBounds(146, 5, 57, 22);
    bpmValue.setBounds(203, 5, 64, 22);
    snapBox.setBounds(270, 5, 100, 22);
    autoButton.setBounds(372, 5, 84, 22);
    autoMode.setBounds(458, 5, 74, 22);
    frameSelector.setBounds(getWidth() - 171, juce::roundToInt(drawingArea().getY()) - 17, 154, 17);
}

bool Bass808Screen::drawingEnabled() const
{
    const auto* param = bass.apvts.getRawParameterValue("oscADraw");
    return mode == Synth && param != nullptr && param->load() >= 0.5f;
}

void Bass808Screen::syncDrawWave()
{
    auto wave = bass.getDrawWave();
    if (wave == visibleDrawWave)
        return;
    visibleDrawWave = std::move(wave);
    frameSelector.clear(juce::dontSendNotification);
    if (visibleDrawWave != nullptr && !visibleDrawWave->frames().empty())
    {
        const auto& frames = visibleDrawWave->frames();
        for (size_t i = 0; i < frames.size(); ++i)
            frameSelector.addItem("Frame " + juce::String(static_cast<int>(i + 1)) + " / " + juce::String(frames[i].time, 3) + " s", static_cast<int>(i + 1));
        selectedFrame = std::min(selectedFrame, frames.size() - 1);
        frameSelector.setSelectedId(static_cast<int>(selectedFrame + 1), juce::dontSendNotification);
    }
    frameSelector.setVisible(usesSynthTimeline() && frameSelector.getNumItems() > 0);
    drawPoints = visibleDrawWave != nullptr ? visibleDrawWave->points() : BassDrawWave::Points {};
    if (frameSelector.getNumItems() > 0) drawPoints = visibleDrawWave->frames()[selectedFrame].points;
    drawUndo.clear();
    drawPending = false;
    strokePoint = -1;
    settleTicks = 3;
    previewDirty = true;
    repaint();
}

void Bass808Screen::rememberDrawWave()
{
    if (drawUndo.size() == 32)
        drawUndo.erase(drawUndo.begin());
    const auto settings = bass.currentSettings();
    drawUndo.push_back({ visibleDrawWave, bass.waveformReference(), drawingEnabled(), settings.attackMs,
                         settings.holdSeconds, settings.decaySeconds, settings.sustain, settings.swellDb,
                         settings.punchSemitones, settings.tuneSemitones, bass.lastNote() });
}

void Bass808Screen::selectSequenceFrame()
{
    if (visibleDrawWave == nullptr || visibleDrawWave->frames().empty()) return;
    selectedFrame = static_cast<size_t>(std::max(0, frameSelector.getSelectedId() - 1));
    selectedFrame = std::min(selectedFrame, visibleDrawWave->frames().size() - 1);
    const auto& frame = visibleDrawWave->frames()[selectedFrame];
    drawPoints = frame.points;
    bass.setReferenceWindow(frame.time, 1.0 / frame.hz);
    syncReference();
    repaint();
}

bool Bass808Screen::editingSequence() const
{
    return visibleDrawWave != nullptr && !visibleDrawWave->frames().empty();
}

Bass808Processor::ReferenceView Bass808Screen::drawingReference() const
{
    auto view = reference;
    if (editingSequence())
    {
        const auto& frames = visibleDrawWave->frames();
        const auto& frame = frames[std::min(selectedFrame, frames.size() - 1)];
        view.start = frame.time;
        view.window = 1.0 / frame.hz;
    }
    return view;
}

void Bass808Screen::autoDraw()
{
    syncDrawWave();
    syncReference();
    if (autoMode.getSelectedId() == 1)
    {
        auto sequence = reference.audio != nullptr ? reference.audio->resynthesize() : nullptr;
        if (sequence == nullptr)
        {
            autoStatus = "No pitched bass found in this reference.";
            repaint();
            return;
        }
        rememberDrawWave();
        bass.setDrawWave(sequence);
        setParameter("oscADraw", 1.0f);
        setParameter("sustain", 1.0f);
        setParameter("swell", 0.0f);
        setParameter("punch", 0.0f);
        const double midi = 69.0 + 12.0 * std::log2(sequence->rootHz() / 440.0);
        const int note = juce::roundToInt(midi);
        bass.setPreviewNote(note);
        setParameter("tune", static_cast<float>(midi - note));
        selectedFrame = 0;
        // Preserve the undo entry while refreshing the sequence frame menu.
        auto history = std::move(drawUndo);
        syncDrawWave();
        drawUndo = std::move(history);
        selectSequenceFrame();
        previewCursor = 0.0;
        autoStatus = "Full " + juce::String(sequence->duration(), 3) + " s";
        repaint();
        return;
    }
    Bass808Reference::Cycle cycle;
    Bass808Reference::Envelope envelope;
    if (reference.audio == nullptr || !reference.audio->findCycle(reference.start, reference.window, cycle)
        || !reference.audio->fitEnvelope(envelope))
    {
        autoStatus = "No stable cycle. Select a sustained part of the reference.";
        repaint();
        return;
    }
    rememberDrawWave();
    drawPoints = cycle.points;
    bass.setDrawWave(std::make_shared<const BassDrawWave>(drawPoints));
    auto history = std::move(drawUndo);
    syncDrawWave();
    drawUndo = std::move(history);
    setParameter("oscADraw", 1.0f);
    setParameter("attack", envelope.attackMs);
    setParameter("hold", envelope.holdSeconds);
    setParameter("decay", envelope.decaySeconds);
    setParameter("sustain", 0.0f);
    setParameter("swell", 0.0f);
    bass.setReferenceWindow(cycle.start, cycle.seconds);
    syncReference();
    autoStatus = "Cycle " + juce::String(1.0 / cycle.seconds, 1) + " Hz";
    repaint();
}

void Bass808Screen::publishDrawWave()
{
    if (!drawPending)
        return;
    visibleDrawWave = visibleDrawWave != nullptr ? visibleDrawWave->editFrame(selectedFrame, drawPoints)
                                                : std::make_shared<const BassDrawWave>(drawPoints);
    bass.setDrawWave(visibleDrawWave);
    drawPending = false;
    settleTicks = 3;
    previewDirty = true;
    repaint();
}

void Bass808Screen::drawTo(juce::Point<float> position)
{
    const auto area = drawingArea();
    if (area.isEmpty())
        return;
    const double fraction = juce::jlimit(0.0f, 1.0f, (position.x - area.getX()) / area.getWidth());
    const int point = juce::roundToInt(BassDrawWave::kSize * (drawViewStart + drawViewWidth * fraction));
    const float value = juce::jlimit(-1.0f, 1.0f, (area.getCentreY() - position.y) / (area.getHeight() * 0.45f));
    if (strokePoint < 0 || strokePoint == point)
        drawPoints[static_cast<size_t>(point % BassDrawWave::kSize)] = value;
    else
    {
        const int step = point > strokePoint ? 1 : -1;
        for (int i = strokePoint + step; i != point + step; i += step)
        {
            const float t = static_cast<float>(i - strokePoint) / static_cast<float>(point - strokePoint);
            drawPoints[static_cast<size_t>(i % BassDrawWave::kSize)] = strokeValue + t * (value - strokeValue);
        }
    }
    strokePoint = point;
    strokeValue = value;
    drawPending = true;
    repaint();
}

juce::Rectangle<float> Bass808Screen::plotArea() const
{
    return getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(18.0f);
}

juce::Rectangle<float> Bass808Screen::timelineArea() const
{
    return plotArea().withTrimmedTop(16.0f).withHeight(54.0f);
}

juce::Rectangle<float> Bass808Screen::referenceArea() const
{
    return plotArea().withTrimmedTop(84.0f).withHeight(23.0f).withTrimmedRight(165.0f);
}

juce::Rectangle<float> Bass808Screen::drawingArea() const
{
    return plotArea().withTrimmedTop(reference.audio != nullptr ? 128.0f : 88.0f);
}

bool Bass808Screen::loadReference(const juce::File& file)
{
    auto audio = Bass808Reference::load(file);
    if (audio == nullptr)
        return false;
    bass.setWaveformReference(std::move(audio));
    syncReference();
    return true;
}

void Bass808Screen::syncReference()
{
    if (drawAttachment == nullptr)
        return;
    const auto next = bass.waveformReference();
    const bool sourceChanged = next.audio != reference.audio;
    if (sourceChanged)
    {
        referenceOverview.clear();
        if (next.audio != nullptr)
            referenceOverview = overview(next.audio->points.size(), [&](size_t i) { return next.audio->points[i]; });
    }
    if (sourceChanged || next.start != reference.start || next.window != reference.window)
        repaint();
    reference = next;
    if (reference.audio != nullptr && editingSequence())
    {
        const auto view = drawingReference();
        bass.setReferenceWindow(view.start, view.window);
        reference = bass.waveformReference();
    }
    const bool showing = usesSynthTimeline() && reference.audio != nullptr;
    clearReferenceButton.setVisible(showing);
    referenceWindow.setVisible(showing);
    referenceWindow.setEnabled(!editingSequence());
    referenceWindow.setTooltip(editingSequence() ? "Length of the selected FULL frame. Choose a frame in the overview or Frame menu."
                                               : "Length of the reference slice stretched to one drawn cycle");
    if (showing)
        referenceWindow.setValue(reference.window * 1000.0, juce::dontSendNotification);
}

void Bass808Screen::moveReferenceTo(float x)
{
    if (reference.audio == nullptr)
        return;
    const auto area = referenceArea();
    const double fraction = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    if (editingSequence())
    {
        const double at = fraction * reference.audio->duration();
        const auto& frames = visibleDrawWave->frames();
        const auto closest = std::min_element(frames.begin(), frames.end(), [at](const auto& a, const auto& b)
                                              { return std::abs(a.time - at) < std::abs(b.time - at); });
        frameSelector.setSelectedId(static_cast<int>(closest - frames.begin() + 1), juce::dontSendNotification);
        selectSequenceFrame();
        return;
    }
    bass.setReferenceWindow(bass.snapTime(fraction * reference.audio->duration(), reference.audio->duration() - reference.window), reference.window);
    syncReference();
}

void Bass808Screen::refreshPreview()
{
    auto clip = BassExport::render(bass.currentSettings(), bass.getSample(), bass.lastNote(), bass.hostTempo(), Bass808Preview::kRate);
    // A blank drawing can keep playing while the first pencil stroke is being made.
    if (clip.getNumSamples() <= 1)
    {
        clip.setSize(2, static_cast<int>(BassExport::kHeldSeconds * Bass808Preview::kRate));
        clip.clear();
    }
    previewClip = std::make_shared<const Bass808Preview::Clip>(std::move(clip));
    const auto* l = previewClip->getReadPointer(0);
    const auto* r = previewClip->getReadPointer(1);
    previewOverview = overview(static_cast<size_t>(previewClip->getNumSamples()),
                               [&](size_t i) { return 0.5f * (l[i] + r[i]); });
    bass.synthPreview().setClip(previewClip);
    previewCursor = std::min(previewCursor, previewClip->getNumSamples() / Bass808Preview::kRate);
    previewDirty = false;
    previewWait = 0;
    repaint();
}

void Bass808Screen::startPreview()
{
    refreshPreview();
    previewPlaying = true;
    bass.synthPreview().setPosition(Bass808Preview::Playing, previewCursor);
    repaint();
}

void Bass808Screen::stopPreview()
{
    if (!previewPlaying && !scrubbing && previewClip == nullptr)
        return;
    if (previewPlaying && !scrubbing)
        previewCursor = bass.synthPreview().position();
    previewPlaying = scrubbing = false;
    bass.synthPreview().setPosition(Bass808Preview::Stopped, previewCursor);
    repaint();
}

void Bass808Screen::scrubTo(float x)
{
    if (previewDirty || previewClip == nullptr)
        refreshPreview();
    const auto area = timelineArea();
    const double end = previewClip->getNumSamples() / Bass808Preview::kRate;
    const double fraction = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    previewCursor = bass.snapTime(fraction * end, end);
    bass.synthPreview().setPosition(Bass808Preview::Scrubbing, previewCursor);
    repaint();
}

void Bass808Screen::timerCallback()
{
    if (drawAttachment != nullptr)
    {
        syncDrawWave();
        publishDrawWave();
        syncReference();
        autoButton.setEnabled(reference.audio != nullptr);
        const double bpm = bass.hostTempo();
        const bool host = bass.tempoFromHost();
        tempoSource.setText(host ? "HOST" : "BPM", juce::dontSendNotification);
        bpmValue.setEditable(false, !host);
        if (!bpmValue.isBeingEdited())
            bpmValue.setText(juce::String(bpm, 2), juce::dontSendNotification);
        snapBox.setSelectedId(bass.snapDivision() == 0 ? 1 : bass.snapDivision(), juce::dontSendNotification);
        if (shownTempo != bpm)
        {
            shownTempo = bpm;
            previewDirty = true;
        }
        const bool enabled = drawingEnabled();
        undoButton.setEnabled(!drawUndo.empty());
        clearButton.setEnabled(enabled);
    }
    // re-render the previews when the knobs, the sample or the note changed (and then held still 100 ms)
    std::vector<float> now;
    for (auto* parameter : bass.getParameters())
        now.push_back(parameter->getValue());
    const auto sample = bass.getSample();
    if (now != snapshot || sample != renderedSample || bass.lastNote() != renderedNote)
    {
        snapshot = std::move(now);
        renderedSample = sample;
        renderedNote = bass.lastNote();
        settleTicks = 3;
        previewDirty = true;
        repaint();
    }
    if (settleTicks > 0 && --settleTicks == 0)
    {
        renderPreviews();
        repaint();
    }
    if (usesSynthTimeline() && previewDirty && ++previewWait >= (previewPlaying ? 2 : 3))
        refreshPreview();
    if (previewPlaying && !scrubbing)
    {
        previewCursor = bass.synthPreview().position();
        repaint();
    }

    const bool wasLive = liveActive;
    liveActive = bass.recentPeak() > 0.002f;
    if (liveActive)
        bass.copyScope(live.data(), static_cast<int>(live.size()));
    if ((liveActive || wasLive) && (mode == Synth || mode == Mix || mode == Master))
        repaint();
}

void Bass808Screen::renderPreviews()
{
    settings = bass.currentSettings();
    if (usesSynthTimeline())
        return;
    const int note = renderedNote;
    full = BassEngine::renderNote(settings, renderedSample, note, kSeconds, kRate);
    synthLayer.clear();
    sampleLayer.clear();
    const bool hasSynth = settings.synthLevelDb > -59.5f || settings.oscBLevelDb > BassSettings::kOscBOffDb;
    if (renderedSample != nullptr && settings.sampleLevelDb > -59.5f && hasSynth)
    {
        // each layer with the other one almost silent (not off: the handoff stays as it is)
        auto onlySynth = settings, onlySample = settings;
        onlySynth.sampleLevelDb = -59.0f;
        onlySample.synthLevelDb = -59.0f;
        onlySample.oscBLevelDb = BassSettings::kOscBOffDb;
        synthLayer = BassEngine::renderNote(onlySynth, renderedSample, note, kMixSeconds, kRate);
        sampleLayer = BassEngine::renderNote(onlySample, renderedSample, note, kMixSeconds, kRate);
    }
}

void Bass808Screen::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::deep);
    g.fillRoundedRectangle(bounds, 8.0f);
    juce::ColourGradient glow(colour::teal.withAlpha(0.08f), bounds.getCentreX(), bounds.getCentreY(),
                              juce::Colours::transparentBlack, bounds.getX(), bounds.getY(), true);
    g.setGradientFill(glow);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(colour::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    const auto area = plotArea();
    switch (mode)
    {
        case Synth:
            if (!usesSynthTimeline())
                paintSynth(g, area);
            else
            {
                paintTimeline(g);
                if (reference.audio != nullptr)
                    paintReference(g);
                paintDrawWave(g, drawingArea());
            }
            break;
        case Sample: paintSample(g, area); break;
        case Mix: paintMix(g, area); break;
        case Effects: paintEffects(g, area); break;
        case Master: paintMaster(g, area); break;
    }
}

void Bass808Screen::paintBeatGrid(juce::Graphics& g, juce::Rectangle<float> area, double duration)
{
    if (duration <= 0.0)
        return;
    const double beat = 240.0 / (bass.hostTempo() * bass.beatDenominator());
    const int numerator = bass.beatsPerBar();
    const int division = bass.snapDivision();
    const double step = division > 0 ? std::min(beat, 240.0 / (bass.hostTempo() * division)) : beat;
    const int count = static_cast<int>(std::ceil(duration / step));
    float lastLabel = -100.0f;
    for (int i = 0; i <= count; ++i)
    {
        const double at = i * step;
        if (at > duration)
            break;
        const double beatIndex = at / beat;
        const int nearest = juce::roundToInt(beatIndex);
        const bool wholeBeat = std::abs(beatIndex - nearest) < 1.0e-5;
        const bool bar = wholeBeat && nearest % numerator == 0;
        const float x = area.getX() + area.getWidth() * static_cast<float>(at / duration);
        if (step * area.getWidth() / duration >= 4.0 || wholeBeat)
        {
            g.setColour(colour::tealBright.withAlpha(bar ? 0.65f : wholeBeat ? 0.34f : 0.14f));
            g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        }
        if (wholeBeat && x - lastLabel >= 40.0f && x + 35.0f < area.getRight())
        {
            label(g, juce::String(nearest / numerator + 1) + "." + juce::String(nearest % numerator + 1),
                  { x + 3.0f, area.getBottom() - 12.0f, 38.0f, 12.0f }, colour::text, 10.0f);
            lastLabel = x;
        }
    }
}

void Bass808Screen::paintTimeline(juce::Graphics& g)
{
    const auto area = timelineArea();
    label(g, "SYNTH PREVIEW  " + noteName(bass.lastNote()), area.withY(9.0f).withHeight(16.0f), colour::accentGlow, 12.0f);
    grid(g, area, 0, 2);
    if (previewClip == nullptr)
        return;
    const double end = previewClip->getNumSamples() / Bass808Preview::kRate;
    const double at = previewPlaying && !scrubbing ? bass.synthPreview().position() : previewCursor;
    drawWave(g, previewOverview, 0, previewOverview.size(), area.reduced(0.0f, 2.0f), colour::teal,
             0.86f / peakOf(previewOverview));
    paintBeatGrid(g, area, end);
    const auto ruler = area.withY(area.getY() - 16.0f).withHeight(14.0f);
    label(g, "0.00 s", ruler, colour::textMuted, 11.0f);
    label(g, juce::String(end, 2) + " s", ruler, colour::textMuted, 11.0f, juce::Justification::topRight);
    const float x = area.getX() + area.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, at / end));
    g.setColour(colour::accentGlow);
    g.drawLine(x, area.getY() - 1.0f, x, area.getBottom(), previewPlaying || scrubbing ? 1.8f : 1.0f);
    label(g, juce::String(at, 3) + " s", ruler, colour::accentGlow, 11.0f, juce::Justification::centred);
}

void Bass808Screen::paintReference(juce::Graphics& g)
{
    const auto area = referenceArea();
    const auto end = reference.audio->duration();
    const auto colourRef = juce::Colour::fromRGB(160, 147, 219);
    grid(g, area, 0, 2);
    drawWave(g, referenceOverview, 0, referenceOverview.size(), area, colourRef.withAlpha(0.54f),
             0.85f / peakOf(referenceOverview));
    if (visibleDrawWave != nullptr && !visibleDrawWave->frames().empty())
    {
        g.setColour(colourRef.withAlpha(0.10f));
        g.fillRect(area);
    }
    paintBeatGrid(g, area, end);
    const float left = area.getX() + area.getWidth() * static_cast<float>(reference.start / end);
    const float width = area.getWidth() * static_cast<float>(reference.window / end);
    g.setColour(colourRef.withAlpha(0.32f));
    g.fillRect(area.withX(left).withWidth(std::max(1.0f, width)));
    g.setColour(colourRef);
    g.drawVerticalLine(juce::roundToInt(left), area.getY(), area.getBottom());
    const auto caption = area.withY(area.getY() - 13.0f).withHeight(12.0f);
    const auto trim = reference.audio->truncated ? "  /  FIRST 10 S" : "";
    label(g, "REF  " + reference.audio->name + trim, caption.withTrimmedRight(110.0f), colourRef, 10.5f);
    label(g, juce::String(reference.start, 3) + " / " + juce::String(end, 2) + " s", caption,
          colourRef, 10.5f, juce::Justification::topRight);
}

void Bass808Screen::paintDrawWave(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto title = area.withY(area.getY() - 15.0f).withHeight(14.0f);
    label(g, visibleDrawWave != nullptr && !visibleDrawWave->frames().empty() ? "OSC A  /  FRAME" : "OSC A  /  ONE CYCLE",
          title, colour::accentGlow, 11.0f);
    label(g, autoStatus, title.withTrimmedLeft(155.0f), colour::textMuted, 10.5f);
    label(g, juce::String(1.0 / drawViewWidth, 1) + "x", title.withTrimmedRight(frameSelector.isVisible() ? 166.0f : 0.0f),
          colour::tealBright, 11.0f, juce::Justification::topRight);
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    const float fullWidth = area.getWidth() / static_cast<float>(drawViewWidth);
    area = area.withX(area.getX() - fullWidth * static_cast<float>(drawViewStart)).withWidth(fullWidth);
    grid(g, area, 32, 4);
    if (reference.audio != nullptr)
    {
        std::array<float, BassDrawWave::kSize + 1> guide;
        const auto& audio = *reference.audio;
        const auto view = drawingReference();
        float peak = 1.0e-6f;
        double mean = 0.0;
        for (size_t i = 0; i < guide.size(); ++i)
        {
            const double index = audio.rate * (view.start + view.window * i / BassDrawWave::kSize);
            const auto a = std::min(audio.points.size() - 1, static_cast<size_t>(index));
            const auto b = std::min(a + 1, audio.points.size() - 1);
            guide[i] = static_cast<float>(audio.points[a] + (index - a) * (audio.points[b] - audio.points[a]));
            if (i < BassDrawWave::kSize) mean += guide[i] / BassDrawWave::kSize;
        }
        // Match the same DC removal and normalization used when extracting the frame.
        for (size_t i = 0; i < guide.size(); ++i)
        {
            if (editingSequence()) guide[i] -= static_cast<float>(mean);
            if (i < BassDrawWave::kSize) peak = std::max(peak, std::abs(guide[i]));
        }
        drawLine(g, guide.data(), static_cast<int>(guide.size()), area, juce::Colour::fromRGB(160, 147, 219).withAlpha(0.54f),
                 1.2f, 0.9f / peak);
    }
    if (!drawingEnabled())
    {
        if (reference.audio != nullptr)
            label(g, "Enable DRAW to trace the reference", area.withTrimmedTop(5.0f), colour::textMuted, 12.0f,
                  juce::Justification::centredTop);
        else if (previewClip != nullptr)
        {
            const auto* wave = previewClip->getReadPointer(0);
            const double hz = BassEngine::noteHz(bass.lastNote(), settings.tuneSemitones);
            const int count = previewClip->getNumSamples();
            const int start = std::min(count / 3, static_cast<int>(0.20 * Bass808Preview::kRate));
            const int visible = std::min(count - start, static_cast<int>(3.0 * Bass808Preview::kRate / hz));
            drawLine(g, wave + start, visible, area, colour::accentGlow, 1.4f, 0.9f / peakOf(previewOverview));
        }
        return;
    }
    std::array<float, BassDrawWave::kSize + 1> cycle;
    std::copy(drawPoints.begin(), drawPoints.end(), cycle.begin());
    cycle.back() = cycle.front();
    drawLine(g, cycle.data(), static_cast<int>(cycle.size()), area, colour::accentGlow, 1.7f, 1.0f);
    if (reference.audio == nullptr && std::all_of(drawPoints.begin(), drawPoints.end(), [](float x) { return x == 0.0f; }))
        label(g, "Draw a wave here. Press PLAY or hold a MIDI note.", area.withTrimmedTop(14.0f),
              colour::textMuted, 13.0f, juce::Justification::centredTop);
}

void Bass808Screen::paintSynth(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto noteArea = area.removeFromLeft(area.getWidth() * 0.66f);
    area.removeFromLeft(14.0f);
    const auto cycleArea = area;
    grid(g, noteArea, 8, 4);
    grid(g, cycleArea, 3, 4);

    const double hz = BassEngine::noteHz(renderedNote, settings.tuneSemitones);
    if (!full.empty())
    {
        const float scale = 0.95f / peakOf(full);
        drawWave(g, full, 0, full.size(), noteArea, colour::accentGlow, scale);
        // up close: three periods of the body, 0.3 s in (past the punch)
        const int period = juce::jmax(2, static_cast<int>(kRate / hz));
        const size_t from = std::min(full.size() - 1, static_cast<size_t>(0.3 * kRate));
        size_t start = from;
        while (start + 1 < full.size() && start < from + static_cast<size_t>(period) && !(full[start] <= 0.0f && full[start + 1] > 0.0f))
            ++start;
        const int count = std::min(3 * period, static_cast<int>(full.size() - start));
        drawLine(g, full.data() + start, count, cycleArea, colour::accentGlow.withAlpha(liveActive ? 0.35f : 1.0f), 1.6f, scale);

        const double t6 = timeToDrop(full, kRate, 6.0), t24 = timeToDrop(full, kRate, 24.0);
        const auto seconds = [](double t) { return t < 0.0 ? juce::String(">1.6 s") : juce::String(t, 2) + " s"; };
        label(g, "-6 dB " + seconds(t6) + "    -24 dB " + seconds(t24), noteArea.withHeight(16.0f).translated(0.0f, noteArea.getHeight() - 14.0f),
              colour::textMuted, 11.0f, juce::Justification::bottomRight);
    }
    if (liveActive)
    {
        // the live output, three periods of the note, from a rising zero crossing
        const double rate = bass.getSampleRate() > 0.0 ? bass.getSampleRate() : 48000.0;
        const int period = juce::jmax(2, static_cast<int>(rate / BassEngine::noteHz(bass.lastNote(), settings.tuneSemitones)));
        const int count = std::min(3 * period, static_cast<int>(live.size()) / 2);
        int start = static_cast<int>(live.size()) - count - 1;
        for (int i = start; i > start - period && i > 0; --i)
            if (live[static_cast<size_t>(i)] <= 0.0f && live[static_cast<size_t>(i + 1)] > 0.0f)
            {
                start = i;
                break;
            }
        drawLine(g, live.data() + start, count, cycleArea, colour::tealBright, 1.6f, 0.95f / juce::jmax(0.05f, bass.recentPeak()));
        label(g, "LIVE", cycleArea.withHeight(16.0f), colour::tealBright, 11.0f, juce::Justification::topRight);
    }

    static const char* waves[] { "SINE", "TRIANGLE", "SAW", "SQUARE", "REESE" };
    label(g, juce::String("NOTE  ") + noteName(renderedNote) + "  " + juce::String(hz, 1) + " Hz   " + waves[juce::jlimit(0, 4, settings.wave)],
          getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::text, 12.0f);
    if (drawAttachment == nullptr)
        label(g, "WAVEFORM", cycleArea.withY(8.0f).withHeight(16.0f), colour::textMuted, 11.0f);
    label(g, "1.6 s", noteArea.withHeight(14.0f).translated(0.0f, -16.0f), colour::textMuted, 11.0f, juce::Justification::topRight);
}

float Bass808Screen::markerValue(int marker) const
{
    return bass.apvts.getRawParameterValue(markerId(marker))->load();
}

const char* Bass808Screen::markerId(int marker) const
{
    return marker == 0 ? "smpStart" : (marker == 1 ? "smpLoopStart" : "smpEnd");
}

int Bass808Screen::markerAt(juce::Point<float> p) const
{
    const auto area = plotArea();
    int best = -1;
    float distance = 9.0f;
    for (int m = 0; m < 3; ++m)
    {
        if (m == 1 && bass.apvts.getRawParameterValue("smpLoop")->load() < 0.5f)
            continue;
        const float x = area.getX() + area.getWidth() * markerValue(m);
        if (std::abs(p.x - x) < distance)
        {
            distance = std::abs(p.x - x);
            best = m;
        }
    }
    return best;
}

void Bass808Screen::paintSample(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto sample = renderedSample;
    if (sample == nullptr)
    {
        auto box = area.reduced(40.0f, 20.0f);
        juce::Path outline;
        outline.addRoundedRectangle(box, 10.0f);
        juce::Path dashed;
        const float dashes[] { 8.0f, 6.0f };
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(colour::teal);
        g.fillPath(dashed);
        label(g, "DROP A WAV HERE  or press LOAD", box.withTrimmedBottom(box.getHeight() * 0.45f), colour::textBright, 18.0f,
              juce::Justification::centredBottom);
        label(g, "a kick becomes an 808: loop its body, play it on the keys, stretch it - or layer any bass one-shot under the synth",
              box.withTrimmedTop(box.getHeight() * 0.6f), colour::textMuted, 12.5f, juce::Justification::centredTop);
        return;
    }
    grid(g, area, 10, 4);
    const auto& data = sample->data;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.47f;
    const int columns = static_cast<int>(area.getWidth());
    const float start = markerValue(0), end = markerValue(2), loop = markerValue(1);
    const bool looping = bass.apvts.getRawParameterValue("smpLoop")->load() >= 0.5f;
    for (int x = 0; x < columns; ++x)
    {
        const float position = static_cast<float>(x) / static_cast<float>(columns);
        const size_t a = data.size() * static_cast<size_t>(x) / static_cast<size_t>(columns);
        const size_t b = std::max(a + 1, data.size() * static_cast<size_t>(x + 1) / static_cast<size_t>(columns));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < data.size(); ++i)
        {
            lo = std::min(lo, data[i]);
            hi = std::max(hi, data[i]);
        }
        const bool inside = position >= start && position <= end;
        auto c = inside ? colour::accentGlow : colour::textMuted.withAlpha(0.35f);
        if (inside && looping && position >= loop)
            c = colour::tealBright;
        g.setColour(c);
        g.drawVerticalLine(juce::roundToInt(area.getX()) + x, mid - hi * half, mid - lo * half + 1.0f);
    }
    if (looping)
    {
        const float x0 = area.getX() + area.getWidth() * loop, x1 = area.getX() + area.getWidth() * end;
        g.setColour(colour::teal.withAlpha(0.16f));
        g.fillRect(juce::Rectangle<float>(x0, area.getY(), x1 - x0, area.getHeight()));
    }
    static const char* names[] { "START", "LOOP", "END" };
    for (int m = 0; m < 3; ++m)
    {
        if (m == 1 && !looping)
            continue;
        const float x = area.getX() + area.getWidth() * markerValue(m);
        const auto c = m == 1 ? colour::tealBright : colour::textBright;
        g.setColour(c.withAlpha(m == hovering || m == dragging ? 1.0f : 0.75f));
        g.drawLine(x, area.getY(), x, area.getBottom(), m == hovering || m == dragging ? 2.0f : 1.2f);
        juce::Path flag;
        flag.addTriangle(x - 6.0f, area.getY(), x + 6.0f, area.getY(), x, area.getY() + 8.0f);
        g.fillPath(flag);
        const bool left = m == 2; // END reads to the left of its line
        label(g, names[m], juce::Rectangle<float>(left ? x - 54.0f : x + 4.0f, area.getBottom() - 14.0f, 50.0f, 14.0f), c, 10.5f,
              left ? juce::Justification::topRight : juce::Justification::topLeft);
    }

    juce::String info = sample->name + "   " + juce::String(sample->lengthSeconds(), 2) + " s   ";
    if (sample->rootHz > 0.0)
    {
        const double note = 69.0 + 12.0 * std::log2(sample->rootHz / 440.0);
        const int nearest = juce::roundToInt(note);
        const int cents = juce::roundToInt((note - nearest) * 100.0);
        info += "pitch " + noteName(nearest) + (cents != 0 ? juce::String(cents > 0 ? " +" : " ") + juce::String(cents) + " ct" : juce::String())
                + "  (" + juce::String(sample->rootHz, 1) + " Hz)";
    }
    else
    {
        info += "no pitch found: played as C2";
    }
    label(g, info, getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::text, 12.0f);
    label(g, "drag START / LOOP / END", getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::textMuted, 11.0f, juce::Justification::topRight);
}

void Bass808Screen::paintMix(juce::Graphics& g, juce::Rectangle<float> area)
{
    grid(g, area, 6, 4);
    const size_t count = static_cast<size_t>(kMixSeconds * kRate);
    if (!full.empty())
    {
        const float scale = 0.95f / peakOf(full);
        if (!synthLayer.empty())
        {
            drawWave(g, synthLayer, 0, std::min(count, synthLayer.size()), area, colour::teal, scale);
            drawWave(g, sampleLayer, 0, std::min(count, sampleLayer.size()), area, colour::text.withAlpha(0.7f), scale);
        }
        juce::Path sum;
        const int columns = static_cast<int>(area.getWidth());
        for (int x = 0; x < columns; ++x)
        {
            const size_t a = std::min(full.size() - 1, count * static_cast<size_t>(x) / static_cast<size_t>(columns));
            const size_t b = std::min(full.size(), std::max(a + 1, count * static_cast<size_t>(x + 1) / static_cast<size_t>(columns)));
            float hi = 0.0f;
            for (size_t i = a; i < b; ++i)
                hi = std::max(hi, std::abs(full[i]));
            const float y = area.getCentreY() - juce::jlimit(0.0f, 1.0f, hi * scale) * area.getHeight() * 0.5f;
            if (x == 0)
                sum.startNewSubPath(area.getX(), y);
            else
                sum.lineTo(area.getX() + static_cast<float>(x), y);
        }
        g.setColour(colour::accentGlow);
        g.strokePath(sum, juce::PathStrokeType(1.8f));
    }
    const float handoff = bass.apvts.getRawParameterValue("handoff")->load();
    const float x = area.getX() + area.getWidth() * static_cast<float>(handoff * 0.001 / kMixSeconds);
    if (handoff >= 0.5f)
    {
        g.setColour(colour::textBright.withAlpha(dragging == 3 ? 1.0f : 0.7f));
        g.drawLine(x, area.getY(), x, area.getBottom(), dragging == 3 ? 2.0f : 1.2f);
        label(g, "HANDOFF " + juce::String(juce::roundToInt(handoff)) + " ms", juce::Rectangle<float>(x + 5.0f, area.getY(), 140.0f, 14.0f),
              colour::textBright, 11.0f);
    }
    const auto top = getLocalBounds().toFloat().reduced(14.0f, 8.0f);
    if (renderedSample == nullptr)
        label(g, "LAYERS   no sample loaded: the synth alone (load one on the SAMPLE tab)", top, colour::text, 12.0f);
    else
    {
        label(g, "LAYERS   ", top, colour::text, 12.0f);
        label(g, "synth", top.withTrimmedLeft(70.0f), colour::tealBright, 12.0f);
        label(g, "sample", top.withTrimmedLeft(112.0f), colour::textBright, 12.0f);
        label(g, "sum (envelope)", top.withTrimmedLeft(164.0f), colour::accentGlow, 12.0f);
        label(g, handoff >= 0.5f ? "drag the handoff point" : "layered: drag across to hand the sample over to the synth", top,
              colour::textMuted, 11.0f, juce::Justification::topRight);
    }
    label(g, "0.6 s", area.withTrimmedTop(area.getHeight() - 14.0f), colour::textMuted, 11.0f, juce::Justification::bottomRight);
}

void Bass808Screen::paintEffects(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto filterArea = area.removeFromLeft(area.getWidth() * 0.56f);
    area.removeFromLeft(18.0f);
    auto satArea = area;
    grid(g, filterArea, 8, 6);

    // filter response (the analog prototypes of the voice's SVF), 20 Hz .. 8 kHz, -36 .. +18 dB
    const double hz = BassEngine::noteHz(renderedNote, settings.tuneSemitones);
    const double cutoff = juce::jlimit(16.0, 20000.0, settings.cutoffHz * std::exp2(settings.filterKey * std::log2(hz / Synth808Voice::kC2Hz)));
    const auto response = [&](double f, double fc) -> double
    {
        using C = std::complex<double>;
        const double k = 1.41421356 * (1.0 - settings.resonance) + 0.06 * settings.resonance;
        const auto section = [](C s, double kk) { return 1.0 / (s * s + kk * s + 1.0); };
        if (settings.filterType == BassSettings::Vowel)
        {
            static constexpr double vowels[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            static constexpr double weights[3] { 1.0, 0.6, 0.3 };
            const double v = juce::jlimit(0.0, 4.0, static_cast<double>(settings.vowel));
            const int i = std::min(3, static_cast<int>(v));
            const double t = v - i, kv = 1.0 / (5.0 + 15.0 * settings.resonance);
            C sum = section(C(0.0, f / 150.0), 1.41421356);
            for (int n = 0; n < 3; ++n)
            {
                const double fn = vowels[i][n] * (1.0 - t) + vowels[i + 1][n] * t;
                const C s(0.0, f / fn);
                sum += 2.0 * weights[n] * kv * s * section(s, kv);
            }
            return std::abs(sum);
        }
        const C s(0.0, f / fc);
        switch (settings.filterType)
        {
            case BassSettings::LP24: return std::abs(section(s, k) * section(s, 1.41421356));
            case BassSettings::HP12: return std::abs(s * s * section(s, k));
            case BassSettings::BP: return std::abs(k * s * section(s, k));
            default: return std::abs(section(s, k));
        }
    };
    const auto xOf = [&](double f) { return filterArea.getX() + filterArea.getWidth() * static_cast<float>(std::log(f / 20.0) / std::log(8000.0 / 20.0)); };
    const auto yOf = [&](double db) { return filterArea.getY() + filterArea.getHeight() * static_cast<float>((18.0 - db) / 54.0); };
    const bool open = settings.filterType != BassSettings::Vowel && settings.cutoffHz >= 19999.0f && settings.filterEnvOct == 0.0f && settings.lfoDepth == 0.0f;
    // the note's harmonics: which of them pass
    for (int n = 1; n <= 16; ++n)
    {
        const double f = hz * n;
        if (f > 8000.0)
            break;
        g.setColour((n == 1 ? colour::tealBright : colour::teal).withAlpha(n == 1 ? 0.8f : 0.45f));
        const float x = xOf(f);
        g.drawLine(x, filterArea.getBottom() - (n == 1 ? 26.0f : 14.0f), x, filterArea.getBottom(), n == 1 ? 2.0f : 1.2f);
    }
    const auto curve = [&](double fc, juce::Colour c, float thickness)
    {
        juce::Path path;
        for (int x = 0; x <= static_cast<int>(filterArea.getWidth()); x += 2)
        {
            const double f = 20.0 * std::pow(8000.0 / 20.0, x / filterArea.getWidth());
            const double db = open ? 0.0 : 20.0 * std::log10(std::max(1.0e-6, response(f, fc)));
            const float y = juce::jlimit(filterArea.getY(), filterArea.getBottom(), yOf(db));
            if (x == 0)
                path.startNewSubPath(filterArea.getX(), y);
            else
                path.lineTo(filterArea.getX() + static_cast<float>(x), y);
        }
        g.setColour(c.withAlpha(0.25f));
        g.strokePath(path, juce::PathStrokeType(thickness * 3.0f));
        g.setColour(c);
        g.strokePath(path, juce::PathStrokeType(thickness));
    };
    if (!open && settings.filterType != BassSettings::Vowel && settings.filterEnvOct != 0.0f)
        curve(juce::jlimit(16.0, 20000.0, cutoff * std::exp2(settings.filterEnvOct)), colour::textMuted, 1.0f); // where the envelope starts
    curve(cutoff, colour::accentGlow, 1.8f);
    g.setColour(colour::text.withAlpha(0.18f));
    g.drawHorizontalLine(juce::roundToInt(yOf(0.0)), filterArea.getX(), filterArea.getRight());
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
        label(g, f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : juce::String(f, 0), juce::Rectangle<float>(xOf(f) + 2.0f, filterArea.getBottom() - 12.0f, 30.0f, 12.0f),
              colour::textMuted, 10.0f);
    static const char* types[] { "LP12", "LP24", "HP12", "BP", "VOWEL" };
    const auto top = getLocalBounds().toFloat().reduced(14.0f, 8.0f);
    juce::String title = juce::String("FILTER  ") + types[juce::jlimit(0, 4, settings.filterType)];
    if (open)
        title += "  open";
    else if (settings.filterType != BassSettings::Vowel)
        title += "  " + juce::String(juce::roundToInt(cutoff)) + " Hz at " + noteName(renderedNote);
    label(g, title, top, colour::text, 12.0f);

    // the saturator: transfer curve and harmonics, from its own code
    juce::String satTitle = "SATURATOR  off";
    if (settings.satModel > 0)
    {
        static const char* models[] { "Off", "Classic", "Tape", "Tube", "Iron", "Germanium", "Vinyl" };
        satTitle = juce::String("SATURATOR  ") + models[juce::jlimit(0, 6, settings.satModel)] + "  " + juce::String(settings.satDriveDb, 1) + " dB";
        const auto params = BassEngine::saturatorParams(settings);
        auto curveArea = satArea.removeFromLeft(satArea.getHeight());
        satArea.removeFromLeft(12.0f);
        grid(g, curveArea, 4, 4);
        std::array<float, 256> in {}, out {};
        BassSaturator::transferCurve(params, in, out);
        float outPeak = 1.0e-6f;
        for (auto v : out)
            outPeak = std::max(outPeak, std::abs(v));
        juce::Path transfer;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const float x = curveArea.getCentreX() + in[i] * curveArea.getWidth() * 0.48f;
            const float y = curveArea.getCentreY() - out[i] / outPeak * curveArea.getHeight() * 0.48f;
            if (i == 0)
                transfer.startNewSubPath(x, y);
            else
                transfer.lineTo(x, y);
        }
        g.setColour(colour::accentGlow);
        g.strokePath(transfer, juce::PathStrokeType(1.6f));
        std::array<double, 9> db {};
        BassSaturator::harmonicProfile(params, -12.0, db);
        const float barWidth = satArea.getWidth() / 9.0f;
        for (size_t n = 0; n < db.size(); ++n)
        {
            const float height = juce::jlimit(0.0f, 1.0f, static_cast<float>((db[n] + 72.0) / 72.0)) * satArea.getHeight();
            const auto bar = juce::Rectangle<float>(satArea.getX() + barWidth * static_cast<float>(n) + 2.0f, satArea.getBottom() - height, barWidth - 4.0f, height);
            g.setColour(n == 0 ? colour::tealBright : (n % 2 == 1 ? colour::accentGlow : colour::accent));
            g.fillRect(bar);
            label(g, "H" + juce::String(n + 1), juce::Rectangle<float>(bar.getX(), satArea.getBottom() + 1.0f, barWidth, 12.0f), colour::textMuted, 9.5f);
        }
    }
    else
    {
        label(g, "pick a model: Classic, Tape, Tube,", satArea.withTrimmedBottom(satArea.getHeight() * 0.5f), colour::textMuted, 12.0f,
              juce::Justification::centredBottom);
        label(g, "Iron, Germanium, Vinyl", satArea.withTrimmedTop(satArea.getHeight() * 0.5f + 2.0f), colour::textMuted, 12.0f,
              juce::Justification::centredTop);
    }
    label(g, satTitle, top.withTrimmedLeft(filterArea.getWidth() + 18.0f), colour::text, 12.0f);
}

void Bass808Screen::setParameter(const char* id, float value)
{
    if (auto* parameter = bass.apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void Bass808Screen::paintMaster(juce::Graphics& g, juce::Rectangle<float> area)
{
    // the knobs as they are now (the note previews wait 100 ms; this screen follows at once)
    const auto now = bass.currentSettings();
    auto bandArea = area.removeFromLeft(area.getWidth() * 0.62f);
    area.removeFromLeft(18.0f);
    auto dynArea = area.removeFromLeft(area.getHeight());
    area.removeFromLeft(16.0f);
    const auto meterArea = area;
    grid(g, bandArea, 8, 6);

    // 20 Hz .. 8 kHz, -24 .. +12 dB
    const double low = 20.0, high = 8000.0;
    const auto xOf = [&](double f) { return bandArea.getX() + bandArea.getWidth() * static_cast<float>(std::log(f / low) / std::log(high / low)); };
    const auto yOf = [&](double db) { return bandArea.getY() + bandArea.getHeight() * static_cast<float>((12.0 - db) / 36.0); };
    const double crossover = juce::jlimit(40.0, 2500.0, static_cast<double>(now.masterFreqHz));
    const float split = xOf(crossover);

    // under FREQ: the middle; above it: spread by WIDTH
    g.setColour(colour::accent.withAlpha(0.10f));
    g.fillRect(juce::Rectangle<float>(bandArea.getX(), bandArea.getY(), split - bandArea.getX(), bandArea.getHeight()));
    const float spread = juce::jlimit(0.0f, 1.0f, now.masterWidth / 200.0f);
    if (spread > 0.0f)
    {
        const float half = bandArea.getHeight() * 0.5f * spread;
        juce::ColourGradient wide(colour::tealBright.withAlpha(0.05f), split, 0.0f, colour::tealBright.withAlpha(0.30f), bandArea.getRight(), 0.0f, false);
        g.setGradientFill(wide);
        g.fillRect(juce::Rectangle<float>(split, bandArea.getCentreY() - half, bandArea.getRight() - split, 2.0f * half));
    }
    g.setColour(colour::text.withAlpha(0.35f));
    g.drawVerticalLine(juce::roundToInt(split), bandArea.getY(), bandArea.getBottom());
    label(g, "MONO", juce::Rectangle<float>(bandArea.getX() + 6.0f, bandArea.getY() + 4.0f, 60.0f, 12.0f), colour::accentGlow, 10.5f);
    label(g, now.masterWidth < 0.5f ? juce::String("WIDTH  mono") : "WIDTH  " + juce::String(juce::roundToInt(now.masterWidth)) + " %",
          juce::Rectangle<float>(split + 6.0f, bandArea.getY() + 4.0f, 120.0f, 12.0f), colour::tealBright, 10.5f);

    // the note's harmonics, and where the master's own harmonics reach (DAMPEN)
    const double hz = BassEngine::noteHz(renderedNote, now.tuneSemitones);
    for (int n = 1; n <= 16; ++n)
    {
        const double f = hz * n;
        if (f > high)
            break;
        g.setColour((n == 1 ? colour::tealBright : colour::teal).withAlpha(n == 1 ? 0.8f : 0.45f));
        const float x = xOf(f);
        g.drawLine(x, bandArea.getBottom() - (n == 1 ? 26.0f : 14.0f), x, bandArea.getBottom(), n == 1 ? 2.0f : 1.2f);
    }
    if (now.masterHarmonics > 0.0005f)
    {
        const float from = xOf(std::max(low, 2.0 * hz)), to = xOf(juce::jlimit(low, high, static_cast<double>(now.masterDampenHz)));
        if (to > from)
        {
            const float y = bandArea.getBottom() - 34.0f;
            g.setColour(colour::accentGlow.withAlpha(0.25f + 0.6f * now.masterHarmonics));
            g.fillRect(juce::Rectangle<float>(from, y, to - from, 3.0f));
            static const char* kinds[] { "2ND / 3RD", "EVEN / ODD", "CRUNCHY" };
            label(g, juce::String("HARMONICS  ") + kinds[juce::jlimit(0, 2, now.masterHarmType)], juce::Rectangle<float>(from, y - 13.0f, 160.0f, 12.0f),
                  colour::accentGlow, 10.0f);
        }
    }

    juce::Path path;
    for (int x = 0; x <= static_cast<int>(bandArea.getWidth()); x += 2)
    {
        const double f = low * std::pow(high / low, x / bandArea.getWidth());
        const float y = juce::jlimit(bandArea.getY(), bandArea.getBottom(), yOf(BassMaster::responseDb(now, f)));
        if (x == 0)
            path.startNewSubPath(bandArea.getX(), y);
        else
            path.lineTo(bandArea.getX() + static_cast<float>(x), y);
    }
    g.setColour(colour::accentGlow.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(5.4f));
    g.setColour(colour::accentGlow);
    g.strokePath(path, juce::PathStrokeType(1.8f));
    g.setColour(colour::text.withAlpha(0.18f));
    g.drawHorizontalLine(juce::roundToInt(yOf(0.0)), bandArea.getX(), bandArea.getRight());
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
        label(g, f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : juce::String(f, 0), juce::Rectangle<float>(xOf(f) + 2.0f, bandArea.getBottom() - 12.0f, 30.0f, 12.0f),
              colour::textMuted, 10.0f);

    static const char* subModes[] { "off", "cut", "boost" };
    juce::String title = juce::String("MASTER   sub ") + subModes[juce::jlimit(0, 2, now.masterSub)];
    if (now.masterSub != 0)
        title += " " + juce::String(juce::roundToInt(now.masterSubHz)) + " Hz";
    title += "   mono under " + juce::String(juce::roundToInt(crossover)) + " Hz";
    if (BassMaster::isNeutral(now))
        title += "   (off: the sound passes untouched)";
    label(g, title, getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::text, 12.0f);

    // dynamics: output level against input level, -60 .. 0 dB, and the gain while a note plays
    grid(g, dynArea, 4, 4);
    const auto dx = [&](double db) { return dynArea.getX() + dynArea.getWidth() * static_cast<float>((db + 60.0) / 60.0); };
    const auto dy = [&](double db) { return dynArea.getBottom() - dynArea.getHeight() * static_cast<float>((juce::jlimit(-60.0, 6.0, db) + 60.0) / 66.0); };
    g.setColour(colour::text.withAlpha(0.18f));
    g.drawLine(dx(-60.0), dy(-60.0), dx(0.0), dy(0.0), 1.0f);
    juce::Path transfer;
    for (int i = 0; i <= 60; ++i)
    {
        const double in = -60.0 + i, out = in + BassMaster::dynamicsGainDb(now, in);
        if (i == 0)
            transfer.startNewSubPath(dx(in), dy(out));
        else
            transfer.lineTo(dx(in), dy(out));
    }
    const bool dynamicsOn = std::abs(now.masterDynamics) >= 0.005f;
    g.setColour(dynamicsOn ? colour::accentGlow : colour::textMuted);
    g.strokePath(transfer, juce::PathStrokeType(1.8f));
    if (dynamicsOn)
    {
        g.setColour(colour::tealBright.withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(dx(now.masterThresholdDb)), dynArea.getY(), dynArea.getBottom());
    }
    label(g, dynamicsOn ? (now.masterDynamics < 0.0f ? "DYNAMICS  comp" : "DYNAMICS  expand") : "DYNAMICS  off",
          dynArea.withY(getLocalBounds().toFloat().getY() + 8.0f).withHeight(16.0f), colour::text, 12.0f);
    if (liveActive && dynamicsOn)
        label(g, juce::String(bass.masterGainDb(), 1) + " dB", dynArea.reduced(4.0f), colour::tealBright, 11.0f, juce::Justification::bottomRight);

    // left / right correlation: 1 mono, 0 wide, under 0 the sides cancel in mono
    label(g, "CORRELATION", meterArea.withY(getLocalBounds().toFloat().getY() + 8.0f).withHeight(16.0f), colour::text, 12.0f);
    auto bar = meterArea.withSizeKeepingCentre(std::min(26.0f, meterArea.getWidth()), meterArea.getHeight() - 30.0f).translated(0.0f, -6.0f);
    g.setColour(colour::panel);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(colour::panelEdge);
    g.drawRoundedRectangle(bar, 3.0f, 1.0f);
    g.drawHorizontalLine(juce::roundToInt(bar.getCentreY()), bar.getX() - 4.0f, bar.getRight() + 4.0f);
    label(g, "+1", juce::Rectangle<float>(bar.getRight() + 6.0f, bar.getY() - 2.0f, 24.0f, 12.0f), colour::textMuted, 10.0f);
    label(g, "0", juce::Rectangle<float>(bar.getRight() + 6.0f, bar.getCentreY() - 6.0f, 24.0f, 12.0f), colour::textMuted, 10.0f);
    label(g, "-1", juce::Rectangle<float>(bar.getRight() + 6.0f, bar.getBottom() - 10.0f, 24.0f, 12.0f), colour::textMuted, 10.0f);
    if (liveActive)
    {
        const float c = juce::jlimit(-1.0f, 1.0f, bass.masterCorrelation());
        const float y = bar.getCentreY() - c * bar.getHeight() * 0.5f;
        g.setColour(c >= 0.0f ? colour::tealBright : colour::accentGlow);
        g.fillRect(juce::Rectangle<float>(bar.getX() + 3.0f, std::min(y, bar.getCentreY()), bar.getWidth() - 6.0f, std::abs(y - bar.getCentreY())));
        label(g, juce::String(c, 2), juce::Rectangle<float>(meterArea.getX(), bar.getBottom() + 4.0f, meterArea.getWidth(), 14.0f), colour::tealBright, 11.0f,
              juce::Justification::centredTop);
    }
    else
    {
        label(g, "play a note", juce::Rectangle<float>(meterArea.getX(), bar.getBottom() + 4.0f, meterArea.getWidth(), 14.0f), colour::textMuted, 10.5f,
              juce::Justification::centredTop);
    }
}

void Bass808Screen::mouseMove(const juce::MouseEvent& event)
{
    if (usesSynthTimeline())
    {
        const bool timeline = timelineArea().contains(event.position)
            || (reference.audio != nullptr && referenceArea().contains(event.position));
        const bool pencil = drawingEnabled() && drawingArea().contains(event.position);
        setMouseCursor(timeline ? juce::MouseCursor::LeftRightResizeCursor
                                : pencil ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        return;
    }
    const int now = mode == Sample ? markerAt(event.position) : -1;
    if (now != hovering)
    {
        hovering = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Bass808Screen::mouseDown(const juce::MouseEvent& event)
{
    if (event.getNumberOfClicks() > 1 && drawingArea().contains(event.position))
        return;
    if (usesSynthTimeline() && event.mods.isLeftButtonDown())
    {
        if (timelineArea().contains(event.position))
        {
            scrubbing = true;
            previewDirty = true;
            scrubTo(event.position.x);
            return;
        }
        if (reference.audio != nullptr && referenceArea().contains(event.position))
        {
            movingReference = true;
            moveReferenceTo(event.position.x);
            return;
        }
    }
    if (drawingEnabled() && event.mods.isLeftButtonDown() && drawingArea().contains(event.position))
    {
        syncDrawWave();
        rememberDrawWave();
        strokePoint = -1;
        drawTo(event.position);
        return;
    }
    dragging = -1;
    dragId = nullptr;
    if (mode == Sample && renderedSample != nullptr)
        dragging = markerAt(event.position);
    else if (mode == Mix)
        dragging = 3;
    if (dragging >= 0)
    {
        dragId = dragging == 3 ? "handoff" : markerId(dragging);
        if (auto* parameter = bass.apvts.getParameter(dragId))
            parameter->beginChangeGesture();
        mouseDrag(event);
    }
}

void Bass808Screen::mouseDrag(const juce::MouseEvent& event)
{
    if (scrubbing)
    {
        scrubTo(event.position.x);
        return;
    }
    if (movingReference)
    {
        moveReferenceTo(event.position.x);
        return;
    }
    if (strokePoint >= 0 && drawingEnabled())
    {
        drawTo(event.position);
        return;
    }
    if (dragId == nullptr)
        return;
    const auto area = plotArea();
    const float position = juce::jlimit(0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth());
    if (dragging == 3)
    {
        setParameter(dragId, position * static_cast<float>(kMixSeconds * 1000.0) < 3.0f ? 0.0f : position * static_cast<float>(kMixSeconds * 1000.0));
    }
    else
    {
        const float start = markerValue(0), end = markerValue(2);
        float value = position;
        if (dragging == 0)
            value = std::min(value, end - 0.01f);
        else if (dragging == 2)
            value = std::max(value, start + 0.01f);
        else
            value = juce::jlimit(start, end - 0.005f, value);
        setParameter(dragId, value);
    }
    repaint();
}

void Bass808Screen::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!usesSynthTimeline() || !drawingArea().contains(event.position))
        return;
    const double delta = wheel.deltaY;
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(delta))
    {
        const double pan = event.mods.isShiftDown() ? delta : wheel.deltaX;
        drawViewStart = juce::jlimit(0.0, 1.0 - drawViewWidth, drawViewStart - pan * drawViewWidth);
    }
    else
    {
        const auto area = drawingArea();
        const double anchor = juce::jlimit(0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth());
        const double phase = drawViewStart + anchor * drawViewWidth;
        drawViewWidth = juce::jlimit(1.0 / 32.0, 1.0, drawViewWidth * std::exp(-delta * 3.0));
        drawViewStart = juce::jlimit(0.0, 1.0 - drawViewWidth, phase - anchor * drawViewWidth);
    }
    repaint();
}

void Bass808Screen::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (usesSynthTimeline() && drawingArea().contains(event.position))
    {
        drawViewStart = 0.0;
        drawViewWidth = 1.0;
        repaint();
    }
}

void Bass808Screen::mouseUp(const juce::MouseEvent&)
{
    if (scrubbing && previewPlaying)
        bass.synthPreview().setPosition(Bass808Preview::Playing, previewCursor);
    scrubbing = movingReference = false;
    publishDrawWave();
    strokePoint = -1;
    if (dragId != nullptr)
        if (auto* parameter = bass.apvts.getParameter(dragId))
            parameter->endChangeGesture();
    dragging = -1;
    dragId = nullptr;
    repaint();
}
} // namespace bbg
