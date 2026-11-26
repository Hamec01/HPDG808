#include "Bass808Editor.h"

#include "Bass808/BassExport.h"
#include "Bass808/BassSettingsIO.h"
#include "BassPresetBrowser.h"

#include "UI/SketchTheme.h"

namespace bbg
{
namespace
{
using namespace b808;
using Item = Page::Item;

constexpr int kWidth = Bass808Editor::defaultWidth, kHeight = Bass808Editor::defaultHeight;
constexpr int kHeaderHeight = 64, kTabTop = 70, kTabHeight = 28, kScreenTop = 104, kScreenHeight = 262, kPageTop = 410;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
}
} // namespace

// One quiet strip above the controls, with independent selections for all five modules.
class Bass808Editor::SectionStrip final : public juce::Component
{
public:
    explicit SectionStrip(Bass808Processor& processor) : bass(processor)
    {
        box.setName("Module preset");
        box.setTextWhenNothingSelected("Current settings");
        box.onChange = [this]
        {
            const int index = box.getSelectedId() - 1;
            const auto& factory = bassSectionPresets(section);
            if (juce::isPositiveAndBelow(index, static_cast<int>(factory.size())))
                bass.applySectionPreset(section, factory[static_cast<size_t>(index)].settings);
            else
            {
                const int user = index - static_cast<int>(factory.size());
                if (!juce::isPositiveAndBelow(user, files.size())) return;
                const auto preset = readBassPreset(files[user]);
                if (!preset) { box.setText("Could not read preset", juce::dontSendNotification); return; }
                bass.applySectionPreset(section, preset->settings);
            }
            remember(box.getText());
        };
        save.onClick = [this]
        {
            if (!naming)
            {
                naming = true;
                const auto label = labels[static_cast<size_t>(section)];
                name.setText(label.startsWith("User / ") ? label.substring(7)
                             : (label.isNotEmpty() ? label : "My " + juce::String(bassSectionName(section))), false);
                name.setBounds(box.getBounds()); box.setVisible(false); name.setVisible(true); save.setButtonText("OK");
                if (getPeer() != nullptr) { name.grabKeyboardFocus(); name.selectAll(); }
                return;
            }
            saveCurrent();
        };
        reset.onClick = [this]
        {
            endNaming();
            bass.applySectionPreset(section, bassSectionPresets(section).front().settings);
            remember(bassSectionPresets(section).front().name);
            box.setSelectedId(1, juce::dontSendNotification);
        };
        reset.setTooltip("Reset only this module to its first factory preset");
        save.setTooltip("Save only this module's settings as a reusable preset");
        name.setTextToShowWhenEmpty("Module preset name", b808::colour::textMuted);
        name.onReturnKey = [this] { saveCurrent(); };
        name.onEscapeKey = [this] { endNaming(); };
        for (auto* component : std::array<juce::Component*, 3> { &box, &save, &reset }) addAndMakeVisible(component);
        addChildComponent(name);
        setSection(0);
    }

    void setSection(int index)
    {
        endNaming(); section = index;
        refill(); repaint();
    }
    void clearSelections()
    {
        labels.fill({});
        for (auto& snapshot : snapshots) snapshot.clear();
        synthWave.reset();
        box.setSelectedId(0, juce::dontSendNotification);
        box.setText("Current settings", juce::dontSendNotification);
    }
    void sync()
    {
        const auto i = static_cast<size_t>(section);
        if (labels[i].isEmpty() || naming) return;
        if (snapshots[i] != values() || (section == 0 && synthWave != bass.getDrawWave()))
        {
            labels[i] = {}; snapshots[i].clear();
            box.setSelectedId(0, juce::dontSendNotification);
            box.setText("Custom settings", juce::dontSendNotification);
        }
    }
    void paint(juce::Graphics& g) override
    {
        using namespace b808;
        g.setColour(colour::textMuted);
        g.setFont(juce::FontOptions(11, juce::Font::bold));
        g.drawText(juce::String(bassSectionName(section)).toUpperCase() + " PRESET", 0, 0, 126, getHeight(), juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(11.5f));
        g.drawText("Only this module", 566, 0, getWidth() - 566, getHeight(), juce::Justification::centredRight);
    }
    void resized() override
    {
        box.setBounds(130, 0, 270, getHeight()); name.setBounds(box.getBounds());
        save.setBounds(408, 0, 68, getHeight()); reset.setBounds(484, 0, 68, getHeight());
    }

private:
    juce::File folder() const { return bassUserPresetFolder().getChildFile("Modules").getChildFile(bassSectionName(section)); }
    std::vector<float> values() const
    {
        std::vector<float> result;
        for (const auto& id : bassSectionParameterIds(section))
            if (auto* parameter = bass.apvts.getParameter(id)) result.push_back(parameter->getValue());
        return result;
    }
    void remember(const juce::String& label)
    {
        labels[static_cast<size_t>(section)] = label;
        snapshots[static_cast<size_t>(section)] = values();
        if (section == 0) synthWave = bass.getDrawWave();
    }
    void refill()
    {
        box.clear(juce::dontSendNotification);
        box.addSectionHeading("Factory / " + juce::String(bassSectionName(section)));
        int id = 1;
        for (const auto& preset : bassSectionPresets(section)) box.addItem(preset.name, id++);
        files = folder().findChildFiles(juce::File::findFiles, false, "*.hpbass");
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
        if (!files.isEmpty()) box.addSectionHeading("My module presets");
        for (const auto& file : files) box.addItem("User / " + file.getFileNameWithoutExtension(), id++);
        const auto label = labels[static_cast<size_t>(section)];
        box.setText(label.isEmpty() ? "Current settings" : label, juce::dontSendNotification);
    }
    void endNaming()
    {
        naming = false; name.setVisible(false); box.setVisible(true); save.setButtonText("SAVE AS");
    }
    void saveCurrent()
    {
        const auto typed = juce::File::createLegalFileName(name.getText().trim());
        if (typed.isEmpty()) { endNaming(); return; }
        const auto file = folder().getNonexistentChildFile(typed, ".hpbass");
        BassPresetFile preset;
        preset.name = file.getFileNameWithoutExtension(); preset.settings = bass.currentSettings();
        if (!writeBassPreset(file, preset)) { save.setButtonText("RETRY"); return; }
        remember("User / " + preset.name); endNaming(); refill();
    }
    Bass808Processor& bass;
    int section = 0;
    bool naming = false;
    juce::ComboBox box;
    juce::TextButton save { "SAVE AS" }, reset { "RESET" };
    juce::TextEditor name;
    juce::Array<juce::File> files;
    std::array<juce::String, 5> labels;
    std::array<std::vector<float>, 5> snapshots;
    std::shared_ptr<const BassDrawWave> synthWave;
};

// Holds a note while pressed: hear the sound without a MIDI keyboard (the last played note, C2 at first).
class Bass808Editor::PlayButton final : public juce::TextButton, private juce::Timer
{
public:
    PlayButton(Bass808Processor& processor, Bass808Screen& view) : juce::TextButton("PLAY"), bass(processor), screen(view)
    {
        setClickingTogglesState(true);
        setTooltip("Play/stop a repeating preview of the current sound");
        onClick = [this]
        {
            if (!getToggleState()) { stop(); return; }
            setButtonText("STOP");
            if (screen.usesSynthTimeline())
            {
                screen.startPreview();
                return;
            }
            trigger();
            startTimer(650);
        };
    }
    ~PlayButton() override { stop(); }
    void stop()
    {
        stopTimer();
        screen.stopPreview();
        if (held) bass.keyboardState.noteOff(1, note, 0.0f);
        held = false;
        setToggleState(false, juce::dontSendNotification);
        setButtonText("PLAY");
    }

private:
    void trigger() { note = bass.lastNote(); held = true; bass.keyboardState.noteOn(1, note, 0.9f); }
    void timerCallback() override
    {
        if (held) { bass.keyboardState.noteOff(1, note, 0.0f); held = false; startTimer(350); }
        else { trigger(); startTimer(650); }
    }
    Bass808Processor& bass;
    Bass808Screen& screen;
    int note = 36;
    bool held = false;
};

// Drag it onto a DAW track or a sampler: the sound is rendered to a WAV (Documents/HamloProd/Bass808 Exports)
// as the drag starts, and that file is what drops.
class Bass808Editor::DragButton final : public juce::TextButton
{
public:
    explicit DragButton(Bass808Editor& owner) : juce::TextButton("DRAG"), editor(owner)
    {
        setTooltip("Drag the finished bass sample (the last played note, with the master) onto a track or a sampler");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragging || event.getDistanceFromDragStart() < 6)
            return;
        const auto file = editor.renderToFile(BassExport::exportFolder());
        if (!file.existsAsFile())
            return;
        dragging = true;
        juce::Component::SafePointer<DragButton> self(this);
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this, [self]
        {
            if (self != nullptr)
                self->dragging = false;
        });
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        dragging = false;
        juce::TextButton::mouseUp(event);
    }

private:
    Bass808Editor& editor;
    bool dragging = false;
};

void Bass808Editor::stopAudition() { if (play != nullptr) play->stop(); }

Bass808Editor::Bass808Editor(Bass808Processor& processor)
    : juce::AudioProcessorEditor(processor), bass(processor), screen(processor)
{
#if !HPDG_BASS808_EMBEDDED // inside HPDG the skin stays HPDG's own
    sketch::setActiveSkin(sketch::Skin::Dark); // HPDG's knobs take the Dark skin's colours
#endif
    setLookAndFeel(&look);

    presetButton.setName("Browse presets");
    presetButton.onClick = [this] { endNaming(); presetBrowser->open(); };
    previousPreset.onClick = [this] { stepPreset(-1); };
    nextPreset.onClick = [this] { stepPreset(1); };
    saveButton.setTooltip("Save this sound as a preset (Documents/HamloProd/Bass808 Presets, shared with HPDG)");
    saveButton.onClick = [this] { savePreset(); };
    addAndMakeVisible(presetButton);
    addAndMakeVisible(previousPreset);
    addAndMakeVisible(nextPreset);
    addAndMakeVisible(saveButton);
    nameEditor.setTextToShowWhenEmpty("preset name", juce::Colours::grey);
    nameEditor.onReturnKey = [this] { writePreset(nameEditor.getText()); };
    nameEditor.onEscapeKey = [this] { endNaming(); };
    addChildComponent(nameEditor);

    mode = std::make_unique<ParamCombo>(bass.apvts, "mode", "Mode");
    addAndMakeVisible(*mode);
    play = std::make_unique<PlayButton>(bass, screen);
    addAndMakeVisible(*play);
    exportButton.setTooltip("Save the finished bass sample (the last played note, with the master) as a WAV");
    exportButton.onClick = [this] { exportWav(); };
    addAndMakeVisible(exportButton);
#if !HPDG_BASS808_EMBEDDED // HPDG plays its bass lanes itself
    trioButton.setTooltip("Trio: MIDI channel 1 plays this sound, channel 2 a Mid and channel 3 a Stab preset "
                          "(HPDG's DnB Dancefloor exports its Sub / Mid Bass A / Mid Bass B lanes on channels 1-3)");
    trioButton.onClick = [this] { showTrioMenu(); };
    addAndMakeVisible(trioButton);
#endif
    dragButton = std::make_unique<DragButton>(*this);
    addAndMakeVisible(*dragButton);
    for (const auto& [id, text] : { std::pair { "glide", "Glide" }, { "velocity", "Velocity" }, { "tune", "Tune" }, { "gain", "Gain" } })
    {
        headerKnobs.push_back(std::make_unique<ParamKnob>(bass.apvts, id, text, 34));
        addAndMakeVisible(*headerKnobs.back());
    }

    static const char* tabNames[] { "SYNTH", "SAMPLE", "MIX", "FILTER  /  FX", "MASTER" };
    for (size_t i = 0; i < static_cast<size_t>(tabCount); ++i)
    {
        tabs[i].setButtonText(tabNames[i]);
        tabs[i].setRadioGroupId(808);
        tabs[i].setClickingTogglesState(true);
        tabs[i].onClick = [this, i] { selectTab(static_cast<int>(i)); };
        addAndMakeVisible(tabs[i]);
    }
    addAndMakeVisible(screen);
    screen.onLoadReference = [this] { chooseReference(); };
    screen.onNewPreset = [this]
    {
        endNaming();
        stopAudition();
        bass.newDrawPreset();
        updatePresetName();
        sectionStrip->clearSelections();
    };

    auto& state = bass.apvts;
    pages[0] = std::make_unique<Page>(state, std::vector<Page::Group> {
        // the synth's Level is on MIX (LAYERS): the Envelope's seven knobs need the room here
        { "OSCILLATOR", 2, { { Item::Combo, "wave", "Wave" }, { Item::Knob, "shape", "Shape" },
                             { Item::Combo, "subOct", "Sub Octave" }, { Item::Knob, "sub", "Sub" } } },
        { "PITCH", 2, { { Item::Knob, "punch", "Punch" }, { Item::Knob, "punchTime", "Punch Time" } } },
        { "808 TONE", 2, { { Item::Knob, "drive", "Drive" }, { Item::Knob, "even", "Even" }, { Item::Knob, "saturation", "Saturation" },
                           { Item::Knob, "tone", "Tone" }, { Item::Knob, "toneFall", "Tone Fall" },
                           { Item::Knob, "click", "Click" }, { Item::Knob, "boost", "Boost" } } },
        { "ENVELOPE", 2, { { Item::Knob, "attack", "Attack" }, { Item::Knob, "swell", "Swell" }, { Item::Knob, "swellTime", "Swell Time" },
                           { Item::Knob, "hold", "Hold" }, { Item::Knob, "decay", "Decay" },
                           { Item::Knob, "sustain", "Sustain" }, { Item::Knob, "release", "Release" } } },
    });
    pages[1] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "SAMPLE", 2, { { Item::Knob, "smpLevel", "Level" }, { Item::Knob, "smpTune", "Tune" }, { Item::Knob, "smpFine", "Fine" },
                         { Item::Knob, "smpStretch", "Stretch" }, { Item::Knob, "smpStart", "Start" }, { Item::Knob, "smpEnd", "End" },
                         { Item::Knob, "smpLoopStart", "Loop Start" } } },
        { "FILE", 2, { { Item::Action, nullptr, "LOAD", [this] { chooseSample(); } }, { Item::Action, nullptr, "CLEAR", [this] { bass.clearSample(); } } } },
        { "PLAYBACK", 2, { { Item::Toggle, "smpLoop", "LOOP" }, { Item::Toggle, "smpFollow", "FOLLOW KEY" }, { Item::Toggle, "smpLock", "LOCK TIME" },
                           { Item::Toggle, "smpReverse", "REVERSE" }, { Item::Toggle, "smpInvert", "INVERT" } } },
    });
    constexpr auto synthLevelLabel = "OSC A";
    pages[2] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "LAYERS", 2, { { Item::Knob, "synthLevel", synthLevelLabel }, { Item::Knob, "smpLevel", "Sample" }, { Item::Knob, "handoff", "Handoff" },
                         { Item::Toggle, "smpInvert", "INVERT" } } },
        { "OSC B", 2, { { Item::Knob, "oscBLevel", "Level" }, { Item::Knob, "oscBShape", "Shape" },
                        { Item::Knob, "oscBDecay", "Decay" }, { Item::Knob, "oscBSustain", "Sustain" } } },
        { "WEIGHT", 2, { { Item::Knob, "sub", "Sub" }, { Item::Knob, "phantom", "Phantom" } } },
        { "ENVELOPE", 2, { { Item::Knob, "attack", "Attack" }, { Item::Knob, "swell", "Swell" }, { Item::Knob, "swellTime", "Swell Time" },
                           { Item::Knob, "hold", "Hold" }, { Item::Knob, "decay", "Decay" },
                           { Item::Knob, "sustain", "Sustain" }, { Item::Knob, "release", "Release" } } },
    });
    pages[3] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "FILTER", 2, { { Item::Combo, "fltType", "Type" }, { Item::Knob, "cutoff", "Cutoff" }, { Item::Knob, "reso", "Resonance" },
                         { Item::Knob, "fltKey", "Key" }, { Item::Knob, "fltEnv", "Env" }, { Item::Knob, "fltDecay", "Env Decay" },
                         { Item::Knob, "vowel", "Vowel" } } },
        { "LFO", 2, { { Item::Knob, "lfoRate", "Rate" }, { Item::Combo, "lfoSync", "Sync" }, { Item::Knob, "lfoDepth", "Depth" },
                      { Item::Toggle, "lfoRetrig", "RETRIG" } } },
        { "HAMLOMAG SATURATOR", 2, { { Item::Combo, "satModel", "Model" }, { Item::Knob, "satDrive", "Drive" }, { Item::Knob, "satHarm", "Harmonics" },
                                     { Item::Knob, "satEvenOdd", "Even/Odd" }, { Item::Knob, "satMix", "Mix" }, { Item::Knob, "phantom", "Phantom" } } },
        // after the saturator: keeps a mid bass out of a separate sub's band (DnB Dancefloor's Mid / Stab presets: 110 Hz)
        { "LOW CUT", 1, { { Item::Knob, "lowCut", "Low Cut" } } },
    });
    // the master (BassMaster): the bass made ready for a mix, mono sub and stereo above it
    pages[4] = std::make_unique<Page>(state, std::vector<Page::Group> {
        { "SUB FILTER", 2, { { Item::Combo, "mSub", "Mode" }, { Item::Knob, "mSubHz", "Sub Freq" } } },
        { "BAND  /  WIDTH", 2, { { Item::Knob, "mFreq", "Freq" }, { Item::Knob, "mWidth", "Width" }, { Item::Knob, "mLowGain", "Low Gain" } } },
        { "STEREO HARMONICS", 2, { { Item::Combo, "mHarmType", "Type" }, { Item::Knob, "mHarm", "Amount" },
                                   { Item::Knob, "mHarmSide", "Mid / Side" }, { Item::Knob, "mDampen", "Dampen" } } },
        { "DYNAMICS", 2, { { Item::Knob, "mThresh", "Threshold" }, { Item::Knob, "mDyn", "Amount" },
                           { Item::Knob, "mRelease", "Release" }, { Item::Toggle, "mLimit", "LIMIT" } } },
    });
    for (auto& page : pages)
        addChildComponent(*page);

    sectionStrip = std::make_unique<SectionStrip>(bass);
    addAndMakeVisible(*sectionStrip);
    presetBrowser = std::make_unique<BassPresetBrowser>(bass);
    presetBrowser->onLoaded = [this] { updatePresetName(); sectionStrip->clearSelections(); };
    addChildComponent(*presetBrowser);
    updatePresetName();

    tabs[0].setToggleState(true, juce::dontSendNotification);
    selectTab(0);
    setSize(kWidth, kHeight);
    startTimerHz(10);
}

Bass808Editor::~Bass808Editor()
{
    play.reset(); // PlayButton stops the screen's transport before the screen is destroyed.
    setLookAndFeel(nullptr);
}

void Bass808Editor::showTab(int index)
{
    index = juce::jlimit(0, tabCount - 1, index);
    tabs[static_cast<size_t>(index)].setToggleState(true, juce::dontSendNotification);
    selectTab(index);
}

void Bass808Editor::selectTab(int index)
{
    const bool changedPreview = bass.apvts.getParameter("oscADraw") != nullptr
        && screen.usesSynthTimeline() != (index == Bass808Screen::Synth);
    if (changedPreview)
        stopAudition();
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible(static_cast<int>(i) == index);
    screen.setMode(static_cast<Bass808Screen::Mode>(index));
    if (sectionStrip != nullptr) sectionStrip->setSection(index);
}

void Bass808Editor::updatePresetName()
{
    presetButton.setButtonText(bass.presetName() + "  ...");
    presetButton.setTooltip(bass.presetName() + " — browse, search and filter presets");
}

void Bass808Editor::savePreset()
{
    // the name is typed in place of the preset list (no popup window: inside a host one can open
    // behind the plugin and the save never happens)
    if (!naming)
    {
        naming = true;
        nameEditor.setText(bass.presetName(), juce::dontSendNotification);
        nameEditor.setBounds(presetButton.getBounds());
        presetButton.setVisible(false);
        nameEditor.setVisible(true);
        saveButton.setButtonText("OK");
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        return;
    }
    writePreset(nameEditor.getText());
}

void Bass808Editor::endNaming()
{
    naming = false;
    nameEditor.setVisible(false);
    presetButton.setVisible(true);
    saveButton.setButtonText("SAVE");
}

void Bass808Editor::writePreset(const juce::String& typed)
{
    const auto name = juce::File::createLegalFileName(typed.trim());
    endNaming();
    if (name.isEmpty())
        return;
    BassPresetFile preset;
    preset.name = name;
    preset.settings = bass.currentSettings();
    if (preset.settings.sampleLevelDb > -59.5f)
        preset.sample = bass.getSample();
    const auto file = bassUserPresetFolder().getChildFile(name + ".hpbass");
    if (writeBassPreset(file, preset))
    {
        bass.setUserPresetName(name);
        presetBrowser->refresh();
        updatePresetName();
        message = "Saved: " + name;
    }
    else
    {
        message = "Could not write " + file.getFullPathName();
    }
    messageTicks = 40;
    repaint();
}

juce::File Bass808Editor::renderToFile(const juce::File& folder)
{
    const int note = bass.lastNote();
    const auto buffer = BassExport::render(bass.currentSettings(), bass.getSample(), note, bass.hostTempo());
    const auto file = folder.getChildFile(BassExport::fileName(bass.presetName(), note));
    if (!BassExport::writeWav(file, buffer))
    {
        message = "Could not write " + file.getFullPathName();
        messageTicks = 40;
        repaint();
        return {};
    }
    message = file.getFileName() + "  " + juce::String(buffer.getNumSamples() / BassExport::kRate, 2) + " s";
    messageTicks = 40;
    repaint();
    return file;
}

void Bass808Editor::exportWav()
{
    const auto suggested = BassExport::exportFolder().getChildFile(BassExport::fileName(bass.presetName(), bass.lastNote()));
    BassExport::exportFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser>("Export the bass sample", suggested, "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc)
                         {
                             auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             file = file.withFileExtension("wav");
                             const auto written = renderToFile(file.getParentDirectory());
                             // renderToFile names the file after the preset; keep the name typed in the dialog
                             if (written.existsAsFile() && written != file)
                             {
                                 file.deleteFile();
                                 written.moveFileTo(file);
                                 message = file.getFileName() + "  saved";
                             }
                         });
}

void Bass808Editor::stepPreset(int delta)
{
    presetBrowser->step(delta);
}

juce::String Bass808Editor::trioText() const
{
    const bool on = bass.apvts.getRawParameterValue("trio")->load() >= 0.5f;
    return on ? "TRIO ON" : "TRIO";
}

void Bass808Editor::showTrioMenu()
{
    const auto set = [this](const char* id, float value)
    {
        if (auto* parameter = bass.apvts.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    const bool on = bass.apvts.getRawParameterValue("trio")->load() >= 0.5f;
    juce::PopupMenu menu;
    menu.addItem(1, "Trio (MIDI channels 1 / 2 / 3)", true, on);
    for (int lane = 0; lane < 2; ++lane)
    {
        const char* id = lane == 0 ? "trioMid" : "trioStab";
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(bass.apvts.getParameter(id));
        if (choice == nullptr)
            continue;
        juce::PopupMenu sub;
        for (int i = 0; i < choice->choices.size(); ++i)
            sub.addItem(100 * (lane + 1) + i, choice->choices[i], true, choice->getIndex() == i);
        menu.addSubMenu(lane == 0 ? "Channel 2 - Mid" : "Channel 3 - Stab", sub);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(trioButton), [this, set, on](int result)
    {
        if (result == 1)
            set("trio", on ? 0.0f : 1.0f);
        else if (result >= 100 && result < 200)
            set("trioMid", static_cast<float>(result - 100));
        else if (result >= 200)
            set("trioStab", static_cast<float>(result - 200));
        trioButton.setButtonText(trioText());
    });
}

void Bass808Editor::timerCallback()
{
    if (trioButton.getButtonText() != trioText())
        trioButton.setButtonText(trioText());
    if (presetButton.getButtonText() != bass.presetName() + "  ...")
    {
        updatePresetName();
        sectionStrip->clearSelections();
    }
    sectionStrip->sync();
    if (messageTicks > 0 && --messageTicks == 0)
        repaint();
}

void Bass808Editor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample into HPDG 808", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             if (fc.getResult().existsAsFile())
                                 loadSample(fc.getResult());
                         });
}

void Bass808Editor::loadSample(const juce::File& file)
{
    juce::String error;
    if (bass.loadSample(file, error))
    {
        message = "Loaded " + file.getFileName();
        tabs[1].setToggleState(true, juce::dontSendNotification);
        selectTab(1);
    }
    else
    {
        message = error;
    }
    messageTicks = 40;
    repaint();
}

void Bass808Editor::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser>("Load a waveform reference", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<Bass808Editor> editor(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [editor](const juce::FileChooser& fc)
                         {
                             if (editor != nullptr && fc.getResult().existsAsFile())
                                 editor->loadReference(fc.getResult());
                         });
}

void Bass808Editor::loadReference(const juce::File& file)
{
    message = screen.loadReference(file) ? "Reference loaded: " + file.getFileName()
                                         : "Could not read reference: " + file.getFileName();
    messageTicks = 40;
    repaint();
}

bool Bass808Editor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && isAudioFile(files[0]);
}

void Bass808Editor::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void Bass808Editor::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void Bass808Editor::filesDropped(const juce::StringArray& files, int x, int y)
{
    dragOver = false;
    if (!files.isEmpty())
    {
        if (screen.usesSynthTimeline() && screen.getBounds().contains(x, y))
            loadReference(juce::File(files[0]));
        else
            loadSample(juce::File(files[0]));
    }
}

void Bass808Editor::paint(juce::Graphics& g)
{
    g.fillAll(colour::background);
    // header strip
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).toFloat();
    juce::ColourGradient band(colour::panel, 0.0f, 0.0f, colour::background, 0.0f, header.getBottom(), false);
    g.setGradientFill(band);
    g.fillRect(header);
    g.setColour(colour::panelEdge);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

    g.setColour(colour::accentGlow);
    g.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    g.drawText("HPDG 808", 18, 10, 150, 30, juce::Justification::centredLeft);
    g.setColour(colour::textMuted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("BASS INSTRUMENT  /  HAMLOPROD", 19, 38, 200, 14, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("PRESET", 196, 8, 60, 14, juce::Justification::centredLeft);

    if (messageTicks > 0)
    {
        g.setColour(colour::tealBright);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(message, 400, kHeight - 30, kWidth - 412, 22, juce::Justification::centredRight);
    }
}

void Bass808Editor::paintOverChildren(juce::Graphics& g)
{
    if (!dragOver)
        return;
    g.setColour(colour::accent.withAlpha(0.18f));
    g.fillRect(getLocalBounds());
    g.setColour(colour::accentGlow);
    g.drawRect(getLocalBounds(), 3);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("DROP TO LOAD INTO THE SAMPLE LAYER", getLocalBounds(), juce::Justification::centred);
}

void Bass808Editor::resized()
{
    presetButton.setBounds(196, 24, 206, 28);
    previousPreset.setBounds(406, 24, 26, 28);
    nextPreset.setBounds(434, 24, 26, 28);
    saveButton.setBounds(464, 24, 50, 28);
    mode->setBounds(522, 6, 108, 50);
    play->setBounds(642, 24, 64, 28);
    int x = kWidth - 12 - static_cast<int>(headerKnobs.size()) * 66;
    for (auto& knob : headerKnobs)
    {
        knob->setBounds(x, 2, 66, 60);
        x += 66;
    }
    for (size_t i = 0; i < static_cast<size_t>(tabCount); ++i)
        tabs[i].setBounds(12 + static_cast<int>(i) * 136, kTabTop, 130, kTabHeight);
    dragButton->setBounds(kWidth - 12 - 70, kTabTop, 70, kTabHeight);
    exportButton.setBounds(kWidth - 12 - 70 - 6 - 104, kTabTop, 104, kTabHeight);
    trioButton.setBounds(kWidth - 12 - 70 - 6 - 104 - 6 - 90, kTabTop, 90, kTabHeight);
    screen.setBounds(12, kScreenTop, kWidth - 24, kScreenHeight);
    sectionStrip->setBounds(12, 374, kWidth - 24, 28);
    for (auto& page : pages)
        page->setBounds(12, kPageTop, kWidth - 24, kHeight - kPageTop - 8);
    presetBrowser->setBounds(getLocalBounds());
}
} // namespace bbg
