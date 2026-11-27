// HPDG_Bass808Snapshot <out folder> [preset index]
// Renders the HPDG 808 plugin's window, every tab, to <out>/hpdg808_<tab>.png while a note plays, so the
// look (and the MASTER screen's meters) can be checked without a DAW.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/Bass808/Bass808Editor.h"
#include "../Source/Bass808/Bass808Processor.h"
#include "Bass808/BassExport.h"

using namespace bbg;

int runBass808OscBLab(const juce::File& out);
int runBass808MemphisLab();
int runBass808PresetsLab(const juce::File& out);
int writeGolden(const juce::File& file);
int checkGolden(const juce::File& file);
int runAdvancedRender(const juce::File& spec, const juce::File& out, double bpm);

namespace
{
bool capture(juce::Component& component, const juce::File& file)
{
    juce::FileOutputStream stream(file);
    return stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk()
        && juce::PNGImageFormat().writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), stream);
}

juce::TextButton* findButton(juce::Component& component, const juce::String& name)
{
    if (auto* button = dynamic_cast<juce::TextButton*>(&component); button != nullptr && button->getButtonText() == name)
        return button;
    for (auto* child : component.getChildren())
        if (auto* button = findButton(*child, name))
            return button;
    return nullptr;
}

bool saveDrawExample(const juce::File& out, const BassSettings& settings)
{
    const auto hit = BassExport::render(settings, nullptr, 33, 120.0);
    return hit.getMagnitude(0, hit.getNumSamples()) > 0.05f
        && BassExport::writeWav(out.getChildFile("drawn-A1.wav"), hit);
}

int checkAutoGrid(const juce::File& out)
{
    out.createDirectory();
    auto reference = std::make_shared<Bass808Reference>();
    reference->rate = 24000;
    reference->name = "Auto cycle test";
    reference->points.resize(24000);
    for (size_t i = 0; i < reference->points.size(); ++i)
    {
        const double t = i / reference->rate;
        const double phase = juce::MathConstants<double>::twoPi * (55.0 * t + 1.5 * (1.0 - std::exp(-t / 0.015)));
        reference->points[i] = static_cast<float>((0.6 * std::sin(phase) + 0.15 * std::sin(3 * phase)) * std::exp(-t));
    }
    Bass808Reference::Cycle cycle;
    const bool period = reference->findCycle(0, 0.4534, cycle) && std::abs(1.0 / cycle.seconds - 55.0) < 0.2;
    const bool shortWindow = reference->findCycle(0.4, 0.002, cycle) && std::abs(1.0 / cycle.seconds - 55.0) < 0.2;
    Bass808Reference silence = *reference;
    std::fill(silence.points.begin(), silence.points.end(), 0.0f);
    const bool silent = !silence.findCycle(0, 0.4534, cycle);
    juce::Random random(715);
    for (float& x : silence.points) x = random.nextFloat() - 0.5f;
    const bool noise = !silence.findCycle(0, 0.4534, cycle);

    Bass808Processor bass;
    bass.newDrawPreset();
    bass.setWaveformReference(reference);
    bass.setReferenceWindow(0, 0.4534);
    const auto before = bass.getDrawWave();
    std::unique_ptr<juce::AudioProcessorEditor> editor(bass.createEditor());
    if (auto* button = findButton(*editor, "AUTO DRAW"))
        for (auto* child : button->getParentComponent()->getChildren())
            if (auto* box = dynamic_cast<juce::ComboBox*>(child); box != nullptr && box->getName() == "Auto Draw mode")
                box->setSelectedId(2, juce::dontSendNotification);
    auto* automatic = findButton(*editor, "AUTO DRAW");
    auto* undo = findButton(*editor, "UNDO");
    if (automatic == nullptr || undo == nullptr) return 1;
    automatic->onClick();
    const auto after = bass.getDrawWave();
    const bool converted = after != before && bass.waveformReference().window < 0.025 && bass.currentSettings().drawWaveEnabled
        && bass.currentSettings().sustain == 0.0f;
    undo->onClick();
    const bool undone = bass.getDrawWave() == before && std::abs(bass.waveformReference().window - 0.4534) < 1.0e-6
        && bass.currentSettings().sustain == 1.0f;
    automatic->onClick();
    const bool exported = saveDrawExample(out, bass.currentSettings());
    auto shortReference = std::make_shared<Bass808Reference>();
    shortReference->rate = 24000;
    shortReference->name = "Short 808 with trailing silence";
    shortReference->points.resize(48000, 0.0f);
    for (size_t i = 0; i < 14400; ++i)
    {
        const double t = i / shortReference->rate;
        shortReference->points[i] = static_cast<float>(0.7 * std::sin(juce::MathConstants<double>::twoPi * 55.0 * t) * std::exp(-25.0 * t));
    }
    bass.setWaveformReference(shortReference);
    bass.setReferenceWindow(0, 0.35);
    automatic->onClick();
    const auto shortSettings = bass.currentSettings();
    const auto shortExport = BassExport::render(shortSettings, nullptr, 33, 120);
    const bool shortTail = shortSettings.sustain == 0.0f && shortSettings.decaySeconds < 0.04f
        && shortExport.getNumSamples() < 0.6 * 48000
        && BassExport::writeWav(out.getChildFile("short-808-result.wav"), shortExport);
    std::printf("short reference decay %.4f s, exported %.3f s, check %d\n", shortSettings.decaySeconds,
                shortExport.getNumSamples() / 48000.0, shortTail);
    bass.setSnapDivision(16);
    bass.setManualTempo(120);
    const bool snapped = std::abs(bass.snapTime(0.14, 2) - 0.125) < 1.0e-9;
    bass.setSnapDivision(0);
    const bool free = bass.snapTime(0.14, 2) == 0.14;
    bass.setSnapDivision(16);
    bass.setManualTempo(95);
    juce::MemoryBlock state;
    bass.getStateInformation(state);
    Bass808Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const bool saved = restored.hostTempo() == 95 && restored.snapDivision() == 16;

    struct Host : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm(150.0);
            info.setTimeSignature(TimeSignature { 3, 4 });
            return info;
        }
    } host;
    bass.setPlayConfigDetails(0, 2, 48000, 256);
    bass.prepareToPlay(48000, 256);
    const auto process = [&]
    {
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer midi;
        bass.processBlock(audio, midi);
    };
    bass.setPlayHead(&host);
    process();
    const bool hosted = bass.tempoFromHost() && bass.hostTempo() == 150 && bass.beatsPerBar() == 3;
    bass.setPlayHead(nullptr);
    process();
    const bool manual = !bass.tempoFromHost() && bass.hostTempo() == 95 && bass.beatsPerBar() == 4;
    bass.setManualTempo(120);
    if (auto* play = findButton(*editor, "PLAY"))
    {
        play->setToggleState(true, juce::dontSendNotification);
        play->onClick();
        for (int i = 0; i < 40; ++i) process();
    }
    juce::Timer::callAfterDelay(150, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    const bool screenshot = capture(*editor, out.getChildFile("auto-grid.png"));
    auto* canvas = dynamic_cast<Bass808Screen*>(automatic->getParentComponent());
    const auto area = canvas->getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(146.0f);
    const auto eventAt = [&](double fraction)
    {
        const auto time = juce::Time::getCurrentTime();
        const juce::Point<float> pos { area.getX() + static_cast<float>(fraction) * area.getWidth(), area.getCentreY() };
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                               1.0f, 0.0f, 0.0f, 0.0f, 0.0f, canvas, canvas, time, pos, time, 1, false);
    };
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = static_cast<float>(std::log(2.0) / 3.0);
    canvas->mouseWheelMove(eventAt(0.5), wheel);
    const auto original = bass.getDrawWave()->points();
    canvas->mouseDown(eventAt(0.25));
    canvas->mouseUp(eventAt(0.25));
    const auto edited = bass.getDrawWave()->points();
    bool zoom = edited[192] == 0.0f && edited[128] == original[128];
    zoom = zoom && capture(*editor, out.getChildFile("draw-zoom.png"));
    undo->onClick();
    canvas->mouseDoubleClick(eventAt(0.5));
    canvas->mouseDown(eventAt(0.25));
    canvas->mouseUp(eventAt(0.25));
    zoom = zoom && bass.getDrawWave()->points()[128] == 0.0f;
    std::printf("zoomed pencil coordinate and reset %d\n", zoom);
    std::printf("period %d short %d silence %d noise %d auto %d undo %d export %d snap %d free %d state %d host %d manual %d screenshot %d\n",
                period, shortWindow, silent, noise, converted, undone, exported, snapped, free, saved, hosted, manual, screenshot);
    return period && shortWindow && silent && noise && converted && undone && exported && shortTail && snapped && free && saved && hosted && manual && screenshot && zoom ? 0 : 1;
}

int checkFullReference(const juce::File& out, const juce::File& source)
{
    out.createDirectory();
    const auto reference = Bass808Reference::load(source);
    if (reference == nullptr) return 1;
    Bass808Processor bass;
    bass.newDrawPreset();
    bass.setWaveformReference(reference);
    std::unique_ptr<juce::AudioProcessorEditor> editor(bass.createEditor());
    auto* automatic = findButton(*editor, "AUTO DRAW");
    auto* undo = findButton(*editor, "UNDO");
    if (automatic == nullptr || undo == nullptr) return 1;
    automatic->onClick();
    auto settings = bass.currentSettings();
    const auto wave = bass.getDrawWave();
    if (wave == nullptr || wave->frames().size() < 3) return 1;
    const auto render = BassExport::render(settings, nullptr, bass.lastNote(), 120);
    const double seconds = render.getNumSamples() / 48000.0;
    const bool duration = std::abs(seconds - reference->duration()) < 0.025;
    bool evolving = false;
    for (const auto& frame : wave->frames())
        evolving = evolving || frame.points != wave->frames().front().points;
    const bool pitch = wave->frames().front().hz > wave->frames().back().hz * 1.5;
    const bool decay = wave->frames().back().gain < wave->frames()[2].gain * 0.6f;
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < render.getNumSamples(); ++i)
            finite = finite && std::isfinite(render.getSample(c, i));
    const auto presetFile = out.getChildFile("full-reference.hpbass");
    bool preset = writeBassPreset(presetFile, { "Full reference", settings, nullptr });
    const auto read = readBassPreset(presetFile);
    preset = preset && read && read->settings.drawnWave != nullptr
        && read->settings.drawnWave->frames().size() == wave->frames().size();
    if (preset)
    {
        const auto back = BassExport::render(read->settings, nullptr, bass.lastNote(), 120);
        preset = back.getNumSamples() == render.getNumSamples();
        double error = 0.0;
        for (int i = 0; i < std::min(back.getNumSamples(), render.getNumSamples()); ++i)
            error = std::max(error, static_cast<double>(std::abs(back.getSample(0, i) - render.getSample(0, i))));
        std::printf("preset render lengths %d / %d, max sample error %.12f\n", render.getNumSamples(), back.getNumSamples(), error);
        preset = preset && error == 0.0;
    }
    juce::MemoryBlock state;
    bass.getStateInformation(state);
    Bass808Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const bool project = restored.getDrawWave() != nullptr && restored.getDrawWave()->frames().size() == wave->frames().size();
    auto* canvas = dynamic_cast<Bass808Screen*>(automatic->getParentComponent());
    juce::ComboBox* selector = nullptr;
    for (auto* child : canvas->getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(child); box != nullptr && box->getName() == "Sequence frame") selector = box;
    if (selector == nullptr || selector->getNumItems() != static_cast<int>(wave->frames().size())) return 1;
    selector->setSelectedId(4, juce::dontSendNotification);
    selector->onChange();
    const auto rect = canvas->getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(146.0f);
    const auto now = juce::Time::getCurrentTime();
    const auto pos = rect.getCentre();
    const juce::MouseEvent pen(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                              1, 0, 0, 0, 0, canvas, canvas, now, pos, now, 1, false);
    canvas->mouseDown(pen);
    canvas->mouseUp(pen);
    const auto edited = bass.getDrawWave();
    const bool edit = edited->frames().size() == wave->frames().size()
        && edited->frames()[0].points == wave->frames()[0].points
        && edited->frames()[3].points != wave->frames()[3].points;
    undo->onClick();
    const bool undid = bass.getDrawWave() == wave;
    // Reproduce a restored or manually widened reference window in a FULL editor.
    bass.setReferenceWindow(0.0, reference->duration());
    auto* play = findButton(*editor, "PLAY");
    if (play != nullptr) { play->setToggleState(true, juce::dontSendNotification); play->onClick(); }
    juce::Timer::callAfterDelay(150, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    const auto alignedView = bass.waveformReference();
    const bool aligned = std::abs(alignedView.start - wave->frames()[3].time) < 1.0e-9
        && std::abs(alignedView.window - 1.0 / wave->frames()[3].hz) < 1.0e-9;
    std::printf("FULL reference overlay aligned to selected frame %d\n", aligned);
    const bool image = capture(*editor, out.getChildFile("full-reference.png"));
    const bool wav = BassExport::writeWav(out.getChildFile("808-TALK-full.wav"), render);
    juce::String report = "source_seconds=" + juce::String(reference->duration(), 6) + "\nrender_seconds=" + juce::String(seconds, 6)
        + "\nframes=" + juce::String(static_cast<int>(wave->frames().size())) + "\nroot_hz=" + juce::String(wave->rootHz(), 3) + "\n";
    out.getChildFile("metrics.txt").replaceWithText(report);
    std::printf("FULL frames %d, source %.4f s, rendered %.4f s | duration %d timbre %d pitch %d decay %d finite %d preset %d project %d frame edit %d undo %d\n",
                static_cast<int>(wave->frames().size()), reference->duration(), seconds, duration, evolving, pitch, decay, finite, preset, project, edit, undid);
    return duration && evolving && pitch && decay && finite && preset && project && edit && undid && aligned && image && wav ? 0 : 1;
}

int checkDrawWave(const juce::File& out)
{
    using Points = BassDrawWave::Points;
    if (out.createDirectory().failed())
        return 1;
    Bass808Processor bass;
    bass.applyPreset(208); // OSC B and drive engaged before the reset
    juce::MemoryBlock stockState;
    bass.getStateInformation(stockState);
    auto stockTree = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(stockState.getData(), static_cast<int>(stockState.getSize())));
    stockTree.removeChild(stockTree.getChildWithProperty("id", "oscADraw"), nullptr);
    juce::AudioProcessor::copyXmlToBinary(*stockTree.createXml(), stockState);

    std::unique_ptr<juce::AudioProcessorEditor> editor(bass.createEditor());
    auto* newPreset = findButton(*editor, "NEW PRESET");
    auto* clear = findButton(*editor, "CLEAR");
    auto* undo = findButton(*editor, "UNDO");
    if (newPreset == nullptr || clear == nullptr || undo == nullptr)
        return 1;
    auto* canvas = dynamic_cast<Bass808Screen*>(newPreset->getParentComponent());
    if (canvas == nullptr || !newPreset->isVisible())
        return 1;

    newPreset->onClick();
    const auto empty = bass.currentSettings();
    const auto silence = BassEngine::renderNote(empty, nullptr, 33, 0.3, 48000.0);
    const bool blank = empty.drawWaveEnabled && empty.drawnWave != nullptr && empty.drawnWave->points() == Points {}
        && empty.oscBLevelDb == BassSettings::kOscBOffDb && bass.getSample() == nullptr && bass.presetName() == "Untitled"
        && std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0.0f; })
        && capture(*editor, out.getChildFile("new-preset.png"));

    const auto plot = canvas->getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(106.0f);
    const auto time = juce::Time::getCurrentTime();
    const auto pen = [&](int step)
    {
        const float t = static_cast<float>(step) / 64.0f;
        const auto y = plot.getCentreY() - 0.27f * plot.getHeight() * std::sin(juce::MathConstants<float>::twoPi * t);
        const juce::Point<float> pos { plot.getX() + (plot.getWidth() - 0.1f) * t, y };
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, canvas, canvas, time, plot.getCentre(), time, 1, step != 0);
    };
    canvas->mouseDown(pen(0));
    for (int step = 1; step <= 64; ++step)
        canvas->mouseDrag(pen(step));
    canvas->mouseUp(pen(64));
    const auto drawn = bass.currentSettings();
    const bool stroke = drawn.drawnWave != nullptr && drawn.drawnWave->points() != Points {}
        && saveDrawExample(out, drawn) && capture(*editor, out.getChildFile("drawn-wave.png"));
    clear->onClick();
    const auto cleared = bass.currentSettings();
    const bool flat = cleared.drawWaveEnabled && cleared.drawnWave != nullptr && cleared.drawnWave->points() == Points {}
        && cleared.sustain == drawn.sustain;
    undo->onClick();
    const bool restored = bass.getDrawWave() == drawn.drawnWave;

    juce::MemoryBlock state;
    bass.getStateInformation(state);
    bass.applyPreset(0);
    bass.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto project = bass.currentSettings();
    const bool projectOk = stroke && project.drawWaveEnabled && project.drawnWave != nullptr
        && project.drawnWave->points() == drawn.drawnWave->points() && bass.presetName() == "Untitled";
    const auto preset = out.getChildFile("drawn-example.hpbass");
    const bool wrote = writeBassPreset(preset, { "Drawn example", drawn, nullptr });
    const auto read = wrote ? readBassPreset(preset) : std::nullopt;
    const bool presetOk = stroke && read && read->settings.drawWaveEnabled && read->settings.drawnWave != nullptr
        && read->settings.drawnWave->points() == drawn.drawnWave->points();
    bass.setStateInformation(stockState.getData(), static_cast<int>(stockState.getSize()));
    const bool legacy = !bass.currentSettings().drawWaveEnabled && bass.getDrawWave() == nullptr && bass.getCurrentProgram() == 208;

    Points partial {};
    for (size_t i = 0; i < partial.size(); ++i)
        partial[i] = static_cast<float>(0.6 * std::sin(juce::MathConstants<double>::twoPi * 64.0 * i / partial.size()));
    auto probe = empty;
    probe.drawnWave = std::make_shared<const BassDrawWave>(partial);
    const auto high = BassEngine::renderNote(probe, nullptr, 96, 0.20, 44100.0);
    const auto low = BassEngine::renderNote(probe, nullptr, 33, 0.20, 44100.0);
    const bool bandLimited = std::all_of(high.begin(), high.end(), [](float x) { return std::abs(x) < 1.0e-5f; })
        && std::any_of(low.begin(), low.end(), [](float x) { return std::abs(x) > 0.1f; });

    BassEngine held;
    held.prepare(48000.0, 512);
    held.setSettings(drawn);
    held.noteOn(33, 1.0f);
    std::vector<float> audio(24000);
    held.render(audio.data(), 4000);
    held.setSettings(cleared);
    held.render(audio.data() + 4000, 20000);
    float maxStep = 0.0f;
    for (size_t i = 4000; i < 4500; ++i)
        maxStep = std::max(maxStep, std::abs(audio[i] - audio[i - 1]));
    const bool live = maxStep < 0.025f && std::all_of(audio.end() - 2400, audio.end(), [](float x) { return std::abs(x) < 1.0e-5f; });
    std::printf("new blank %d | pencil stroke and WAV %d | clear %d | undo %d | DAW state %d | preset %d | legacy %d | band limit %d | live clear %d (step %.5f)\n",
                blank, stroke, flat, restored, projectOk, presetOk, legacy, bandLimited, live, maxStep);
    return blank && stroke && flat && restored && projectOk && presetOk && legacy && bandLimited && live ? 0 : 1;
}

int checkSynthTimeline(const juce::File& out)
{
    if (out.createDirectory().failed())
        return 1;
    constexpr int fileRate = 24000, playRate = 44100, block = 256;
    juce::AudioBuffer<float> fixture(2, 12 * fileRate);
    for (int i = 0; i < fixture.getNumSamples(); ++i)
    {
        const double t = static_cast<double>(i) / fileRate;
        const double phase = juce::MathConstants<double>::twoPi * 72.0 * t;
        const float amp = static_cast<float>(0.48 + 0.18 * std::sin(4.0 * t));
        const float value = amp * static_cast<float>(std::sin(phase) + 0.24 * std::sin(2.0 * phase));
        fixture.setSample(0, i, value);
        fixture.setSample(1, i, -value * 0.8f);
    }
    const auto file = out.getChildFile("reference-12-seconds.wav");
    if (!BassExport::writeWav(file, fixture, fileRate))
        return 1;

    Bass808Processor bass;
    bass.setPlayConfigDetails(0, 2, playRate, block);
    bass.prepareToPlay(playRate, block);
    std::unique_ptr<juce::AudioProcessorEditor> owned(bass.createEditor());
    auto* editor = dynamic_cast<Bass808Editor*>(owned.get());
    auto* load = findButton(*editor, "LOAD REF");
    auto* play = findButton(*editor, "PLAY");
    auto* create = findButton(*editor, "NEW PRESET");
    if (load == nullptr || play == nullptr || create == nullptr)
        return 1;
    auto* canvas = dynamic_cast<Bass808Screen*>(load->getParentComponent());
    if (canvas == nullptr)
        return 1;
    create->onClick();
    const auto initialExport = BassExport::render(bass.currentSettings(), bass.getSample(), 36, 120.0);
    const auto middle = canvas->getBounds().getCentre();
    editor->filesDropped({ file.getFullPathName() }, middle.x, middle.y);
    const auto reference = bass.waveformReference();
    const bool crop = reference.audio != nullptr && reference.audio->truncated && reference.audio->duration() == 10.0
        && reference.audio->points.size() == 10 * fileRate && bass.getSample() == nullptr;
    if (!crop)
        return 1;
    bass.setReferenceWindow(0.0, 10.0);
    const bool fullWindow = bass.waveformReference().window == 10.0;
    bass.setReferenceWindow(reference.start, reference.window);
    const auto afterExport = BassExport::render(bass.currentSettings(), bass.getSample(), 36, 120.0);
    const auto process = [&](int blocks)
    {
        juce::AudioBuffer<float> audio(2, block);
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            bass.processBlock(audio, midi);
            peak = std::max(peak, audio.getMagnitude(0, audio.getNumSamples()));
        }
        return peak;
    };
    const auto toggle = [&](bool on)
    {
        play->setToggleState(on, juce::dontSendNotification);
        play->onClick();
    };
    toggle(true);
    const bool silent = process(20) == 0.0f && initialExport.getNumSamples() == afterExport.getNumSamples()
        && afterExport.getMagnitude(0, afterExport.getNumSamples()) == 0.0f;
    toggle(false);

    const auto bounds = canvas->getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(18.0f);
    const auto time = juce::Time::getCurrentTime();
    const auto mouse = [&](float x, float y, bool drag)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, y }, juce::ModifierKeys::leftButtonModifier,
                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, canvas, canvas, time, { x, y }, time, 1, drag);
    };
    const auto draw = bounds.withTrimmedTop(128.0f);
    const auto stroke = [&](int step)
    {
        const auto t = static_cast<float>(step) / 64.0f;
        const auto angle = juce::MathConstants<float>::twoPi * t;
        return mouse(draw.getX() + (draw.getWidth() - 0.1f) * t,
                     draw.getCentreY() - draw.getHeight() * (0.26f * std::sin(angle) + 0.07f * std::sin(3.0f * angle)), step != 0);
    };
    canvas->mouseDown(stroke(0));
    for (int i = 1; i <= 64; ++i)
        canvas->mouseDrag(stroke(i));
    canvas->mouseUp(stroke(64));
    const auto shape = bass.getDrawWave()->points();
    toggle(true);
    const bool audible = process(30) > 0.04f && bass.synthPreview().position() > 0.14;

    const float y = bounds.getY() + 30.0f;
    canvas->mouseDown(mouse(bounds.getX() + 0.3f * bounds.getWidth(), y, false));
    const bool seek = process(6) > 0.04f && bass.synthPreview().position() > 0.5;
    canvas->mouseDrag(mouse(bounds.getX() + 0.65f * bounds.getWidth(), y, true));
    const bool scrub = process(9) > 0.04f && bass.synthPreview().position() > 1.1;
    canvas->mouseUp(mouse(bounds.getX() + 0.65f * bounds.getWidth(), y, false));
    const bool resumed = process(35) > 0.04f && bass.synthPreview().position() > 1.45;
    const bool visual = capture(*editor, out.getChildFile("synth-timeline.png"));
    toggle(false);
    process(2);
    const auto referenceArea = bounds.withTrimmedRight(165.0f);
    const auto pick = mouse(referenceArea.getX() + 0.27f * referenceArea.getWidth(), bounds.getY() + 93.0f, false);
    canvas->mouseDown(pick);
    canvas->mouseUp(pick);
    const auto selection = bass.waveformReference();
    const bool selected = selection.start > 2.6 && selection.start < 2.8 && process(24) == 0.0f
        && bass.getDrawWave()->points() == shape;
    const auto seekOnly = mouse(bounds.getX() + 0.24f * bounds.getWidth(), y, false);
    canvas->mouseDown(seekOnly);
    canvas->mouseUp(seekOnly);
    const bool scrubStopped = process(6) > 0.04f && process(25) > 0.01f && process(12) == 0.0f;

    juce::MemoryBlock state;
    bass.getStateInformation(state);
    Bass808Processor reloaded;
    reloaded.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto saved = reloaded.waveformReference();
    const bool persist = saved.audio != nullptr && saved.audio->duration() == 10.0 && saved.audio->truncated
        && std::abs(saved.start - selection.start) < 1.0e-5 && reloaded.getSample() == nullptr;
    std::printf("reference capped %d | full window %d | reference silent and export unchanged %d | Play and cursor %d | seek %d | scrub %d | resumed %d | reference selection %d | stopped scrub %d | project state %d | screenshot %d\n",
                crop, fullWindow, silent, audible, seek, scrub, resumed, selected, scrubStopped, persist, visual);
    return fullWindow && silent && audible && seek && scrub && resumed && selected && scrubStopped && persist && visual ? 0 : 1;
}

// Plays a held note through the processor (the scope and the meters fill), then shoots each tab once the
// screen's previews have rendered.
class Shooter final : private juce::Timer
{
public:
    Shooter(Bass808Processor& p, juce::File folder) : bass(p), out(std::move(folder))
    {
        editor.reset(dynamic_cast<Bass808Editor*>(bass.createEditor()));
        editor->setVisible(true);
        editor->showTab(0);
        startTimer(400);
    }

private:
    void play()
    {
        juce::AudioBuffer<float> buffer(2, 512);
        for (int block = 0; block < 40; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0 && !started)
                midi.addEvent(juce::MidiMessage::noteOn(1, 33, 0.9f), 0);
            started = true;
            bass.processBlock(buffer, midi);
        }
    }

    void timerCallback() override
    {
        static const char* names[] { "synth", "sample", "mix", "fx", "master" };
        play();
        if (!settled)
        {
            // one more round: the screen's timer has seen the note and rendered its previews
            settled = true;
            return;
        }
        juce::FileOutputStream stream(out.getChildFile(juce::String("hpdg808_") + names[tab] + ".png"));
        stream.setPosition(0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), stream);
        if (++tab >= 5)
        {
            stopTimer();
            editor.reset();
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        editor->showTab(tab);
        settled = false;
    }

    Bass808Processor& bass;
    juce::File out;
    std::unique_ptr<Bass808Editor> editor;
    int tab = 0;
    bool settled = false, started = false;
};

// Trio (DnB Dancefloor, 2026-11-26): MIDI channel 2 / 3 play the chosen Mid / Stab presets next to the main sound,
// off they go to the main sound; Low Cut removes the lows of a mid preset; a state saved before Trio loads it off.
int checkTrio()
{
    using namespace bbg;
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
        failures += ok ? 0 : 1;
    };
    const auto render = [](Bass808Processor& bass, int channel, int note)
    {
        juce::AudioBuffer<float> buffer(2, 512);
        double energy = 0.0;
        for (int block = 0; block < 60; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.9f), 0);
            if (block == 50)
                midi.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
            bass.processBlock(buffer, midi);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                energy += static_cast<double>(buffer.getSample(0, i)) * buffer.getSample(0, i);
        }
        return energy;
    };
    const auto set = [](Bass808Processor& bass, const char* id, float value)
    {
        if (auto* p = bass.apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    // lows of a buffer: energy under 80 Hz of a held note, by a one-pole low-pass
    const auto lowEnergy = [](const BassSettings& s)
    {
        const auto audio = BassEngine::renderNote(s, nullptr, 29, 1.0, 48000.0);
        double y = 0.0, e = 0.0;
        const double a = 1.0 - std::exp(-2.0 * 3.14159265 * 80.0 / 48000.0);
        for (const float x : audio)
        {
            y += a * (x - y);
            e += y * y;
        }
        return e;
    };

    Bass808Processor bass;
    bass.setPlayConfigDetails(0, 2, 48000.0, 512);
    bass.prepareToPlay(48000.0, 512);
    const double offCh2 = render(bass, 2, 41);
    set(bass, "trio", 1.0f);
    const double onCh1 = render(bass, 1, 41);
    const double onCh2 = render(bass, 2, 41);
    const double onCh3 = render(bass, 3, 41);
    check(offCh2 > 1.0e-6 && onCh1 > 1.0e-6 && onCh2 > 1.0e-6 && onCh3 > 1.0e-6, "every channel sounds");
    check(std::abs(onCh2 - offCh2) > 0.05 * offCh2, "Trio: channel 2 plays another sound than the main one");
    check(std::abs(onCh3 - onCh2) > 0.05 * onCh2, "Trio: channel 3 plays another sound than channel 2");
    set(bass, "trio", 0.0f);
    const double backCh2 = render(bass, 2, 41);
    check(std::abs(backCh2 - offCh2) < 0.02 * offCh2 + 1.0e-9, "Trio off: channel 2 is the main sound again");

    const BassPreset* reese = nullptr;
    for (const auto& preset : bassPresets())
        if (juce::String(preset.name) == "Dancefloor - Mid Reese")
            reese = &preset;
    check(reese != nullptr && reese->settings.lowCutHz > 100.0f, "Mid Reese has its low cut");
    if (reese != nullptr)
    {
        auto open = reese->settings;
        open.lowCutHz = 0.0f;
        check(lowEnergy(reese->settings) < 0.25 * lowEnergy(open), "Low Cut takes the lows out");
    }
    int modern = 0;
    for (const auto& preset : bassPresets())
        modern += juce::String(preset.name).startsWith("Modern - ") ? 1 : 0;
    check(modern == 0, "the old Modern presets left the bank");

    // a state from before Trio / Low Cut: both off
    juce::MemoryBlock state;
    bass.getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        const auto id = child->getStringAttribute("id");
        if (id == "trio" || id == "trioMid" || id == "trioStab" || id == "lowCut")
            xml->removeChildElement(child, true);
        child = next;
    }
    set(bass, "trio", 1.0f);
    set(bass, "lowCut", 200.0f);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    bass.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    check(bass.apvts.getRawParameterValue("trio")->load() < 0.5f && bass.apvts.getRawParameterValue("lowCut")->load() < 0.5f,
          "an older state loads with Trio and Low Cut off");
    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc == 3 && juce::String(argv[1]) == "--preset-browser-check")
        return runBass808PresetsLab(juce::File(juce::String(argv[2])));
    if (argc == 4 && juce::String(argv[1]) == "--full-reference-check")
        return checkFullReference(juce::File(juce::String(argv[2])), juce::File(juce::String(argv[3])));
    if (argc == 3 && juce::String(argv[1]) == "--auto-grid-check")
        return checkAutoGrid(juce::File(juce::String(argv[2])));
    if ((argc == 4 || argc == 5) && juce::String(argv[1]) == "--adv-render")
        return runAdvancedRender(juce::File(juce::String(argv[2])), juce::File(juce::String(argv[3])), argc == 5 ? juce::String(argv[4]).getDoubleValue() : 174.0);
    if (argc == 3 && juce::String(argv[1]) == "--golden-write")
        return writeGolden(juce::File(juce::String(argv[2])));
    if (argc == 3 && juce::String(argv[1]) == "--golden-check")
        return checkGolden(juce::File(juce::String(argv[2])));
    if (argc == 2 && juce::String(argv[1]) == "--trio-check")
        return checkTrio();
    if (argc == 2 && juce::String(argv[1]) == "--memphis-check")
        return runBass808MemphisLab();
    if (argc == 3 && juce::String(argv[1]) == "--osc-b")
        return runBass808OscBLab(juce::File(juce::String(argv[2])));
    if (argc == 3 && juce::String(argv[1]) == "--draw-check")
        return checkDrawWave(juce::File(juce::String(argv[2])));
    if (argc == 3 && juce::String(argv[1]) == "--timeline-check")
        return checkSynthTimeline(juce::File(juce::String(argv[2])));
    const juce::File out(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    Bass808Processor bass;
    bass.setPlayConfigDetails(0, 2, 48000.0, 512);
    bass.prepareToPlay(48000.0, 512);
    if (argc > 2)
        bass.applyPreset(juce::String(argv[2]).getIntValue());
    {
        Shooter shooter(bass, out);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }
    return 0;
}
