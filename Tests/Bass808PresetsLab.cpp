#include <cmath>
#include <cstdio>
#include "Bass808/Bass808Editor.h"
#include "Bass808/BassPresetBrowser.h"
#include "Bass808/BassDrawWave.h"
#include "Bass808/BassExport.h"

namespace
{
template <typename T> T* findNamed(juce::Component& root, const juce::String& name)
{
    if (auto* item = dynamic_cast<T*>(&root); item != nullptr && item->getName() == name) return item;
    for (auto* child : root.getChildren()) if (auto* item = findNamed<T>(*child, name)) return item;
    return nullptr;
}
juce::TextButton* button(juce::Component& root, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::TextButton*>(&root); b != nullptr && b->getButtonText() == text) return b;
    for (auto* child : root.getChildren()) if (auto* b = button(*child, text)) return b;
    return nullptr;
}
bool capture(juce::Component& component, const juce::File& file)
{
    // Let the screen's debounced preview and parameter attachments catch up before visual QA.
    juce::Timer::callAfterDelay(180, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    juce::FileOutputStream stream(file);
    return stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk()
        && juce::PNGImageFormat().writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), stream);
}
}

int runBass808PresetsLab(const juce::File& out)
{
    using namespace bbg;
    out.createDirectory();
    int failures = 0;
    const auto check = [&](bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++failures;
    };
    Bass808Processor bass;
    bass.setPlayConfigDetails(0, 2, 48000, 512);
    bass.prepareToPlay(48000, 512);
    auto seed = bass.programs()[17].settings;
    seed.tuneSemitones = 3; seed.gainDb = -8; seed.mode = BassSettings::Legato; seed.glideMs = 137;
    seed.oscBLevelDb = -19; seed.sampleStart = .12f; seed.sampleEnd = .91f;
    seed.sampleLevelDb = -12; seed.masterWidth = 71; seed.masterLimit = true;
    seed.drawnWave = std::make_shared<const BassDrawWave>(BassDrawWave::Points {});
    std::vector<float> signal(4800);
    for (size_t i = 0; i < signal.size(); ++i) signal[i] = .25f * std::sin(static_cast<float>(i) * .0072f);
    const auto sample = BassSample::fromBuffer(std::move(signal), 48000, "Module isolation test");
    bass.setSampleDirect(sample);
    bass.setUserPresetName("Module isolation test");
    bool isolated = true, finite = true, roundTrip = true, bounds = true;
    int recipes = 0;
    for (int section = 0; section < 5; ++section)
    {
        const auto ids = bassSectionParameterIds(section);
        for (const auto& preset : bassSectionPresets(section))
        {
            bass.writeSettings(seed);
            std::vector<float> before;
            for (auto* p : bass.getParameters()) before.push_back(p->getValue());
            const auto wave = bass.getDrawWave();
            bass.applySectionPreset(section, preset.settings);
            size_t index = 0;
            for (auto* parameter : bass.getParameters())
            {
                auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
                if (p != nullptr && !ids.contains(p->paramID)) isolated &= parameter->getValue() == before[index];
                const float v = parameter->getValue();
                bounds &= std::isfinite(v) && v >= 0 && v <= 1;
                ++index;
            }
            isolated &= bass.getSample() == sample && bass.presetName() == "Module isolation test";
            if (section != 0) isolated &= bass.getDrawWave() == wave;
            const auto rendered = BassExport::render(bass.currentSettings(), sample, 33, 120);
            for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
                for (int i = 0; i < rendered.getNumSamples(); ++i) finite &= std::isfinite(rendered.getSample(channel, i));
            ++recipes;
        }
        BassPresetFile saved;
        saved.name = "Round trip"; saved.settings = seed;
        const auto file = out.getChildFile("section-" + juce::String(section) + ".hpbass");
        const bool written = writeBassPreset(file, saved);
        const auto restored = readBassPreset(file);
        roundTrip &= written && restored.has_value();
        if (restored)
        {
            bass.applySectionPreset(section, restored->settings);
            roundTrip &= std::abs(bass.currentSettings().tuneSemitones - 3) < .01f && bass.getSample() == sample;
        }
    }
    std::printf("Checked %d module recipes\n", recipes);
    check(isolated, "All module presets preserve every parameter outside their scope, loaded audio, name and unrelated drawn wave");
    check(finite && bounds, "Every module recipe renders finite audio with valid parameter values");
    check(roundTrip, "User module presets round-trip through disk without changing global tuning or loaded audio");

    bass.applyPreset(0);
    std::unique_ptr<Bass808Editor> editor(dynamic_cast<Bass808Editor*>(bass.createEditor()));
    auto* browse = findNamed<juce::TextButton>(*editor, "Browse presets");
    check(browse != nullptr, "Editor exposes the new preset browser");
    if (browse == nullptr) return 1;
    browse->onClick();
    BassPresetBrowser* browser = nullptr;
    for (auto* child : editor->getChildren()) if (auto* view = dynamic_cast<BassPresetBrowser*>(child)) browser = view;
    if (browser == nullptr) return 1;
    auto* search = findNamed<juce::TextEditor>(*browser, "Search presets");
    auto* category = findNamed<juce::ComboBox>(*browser, "Preset category");
    auto* character = findNamed<juce::ComboBox>(*browser, "Preset character");
    auto* results = findNamed<juce::ListBox>(*browser, "Preset results");
    if (search == nullptr || category == nullptr || character == nullptr || results == nullptr) return 1;
    const auto setSearch = [&](const juce::String& text) { search->setText(text, false); search->onTextChange(); };
    check(browser->isVisible() && browser->resultCount() >= bass.getNumPrograms(), "All released factory program indices remain available");
    check(capture(*editor, out.getChildFile("preset-browser.png")), "Captured the complete browser layout");
    setSearch("cLeAn STILL air");
    check(browser->resultCount() == 1, "Search is case-insensitive and combines words across metadata");
    results->getListBoxModel()->returnKeyPressed(0);
    check(bass.presetName() == "Clean - Still Air" && !browser->isVisible(), "Enter loads the selected factory sound and closes the browser");
    browser->open(); setSearch("zz-no-such-preset-zz");
    const auto unchanged = bass.presetName();
    browser->step(1);
    check(browser->resultCount() == 0 && bass.presetName() == unchanged, "Empty results cannot load an unrelated sound");
    check(capture(*editor, out.getChildFile("preset-browser-empty.png")), "Captured the empty search state");
    setSearch("");
    category->setText("Reese", juce::dontSendNotification); category->onChange();
    bool onlyReese = browser->resultCount() > 0;
    for (int i = 0, count = browser->resultCount(); i < count; ++i)
    {
        browser->step(1);
        onlyReese &= bass.currentSettings().wave == BassSettings::Reese;
    }
    check(onlyReese, "Navigation respects the category filter");
    category->setSelectedId(1, juce::dontSendNotification); category->onChange();
    character->setText("Warm", juce::dontSendNotification); character->onChange();
    check(browser->resultCount() > 0 && browser->resultCount() < bass.getNumPrograms(), "Character filter narrows the library");
    setSearch("trap");
    check(capture(*editor, out.getChildFile("preset-browser-filtered.png")), "Captured a filtered search");
    check(browser->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) && !browser->isVisible(), "Escape closes the browser from its child controls");
    for (int tab = 0; tab < 5; ++tab)
    {
        editor->showTab(tab);
        auto* local = findNamed<juce::ComboBox>(*editor, "Module preset");
        if (local == nullptr) { ++failures; continue; }
        local->setSelectedId(3, juce::dontSendNotification); local->onChange();
        check(capture(*editor, out.getChildFile("module-" + juce::String(tab) + ".png")), bassSectionName(tab));
    }

    // Favorites use an isolated preference file; the user's actual library is never modified by tests.
    const auto preferences = out.getChildFile("favorites-test.xml");
    if (preferences.existsAsFile()) preferences.deleteFile();
    {
        BassPresetBrowser isolatedBrowser(bass, preferences);
        isolatedBrowser.setBounds(editor->getLocalBounds());
        isolatedBrowser.open();
        const auto beforeFavorite = bass.presetName();
        auto* rows = findNamed<juce::ListBox>(isolatedBrowser, "Preset results");
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> pos { static_cast<float>(rows->getWidth() - 27), 24 };
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                              1, 0, 0, 0, 0, rows, rows, now, pos, now, 1, false);
        rows->getListBoxModel()->listBoxItemClicked(0, event);
        button(isolatedBrowser, "Favorites")->onClick();
        check(isolatedBrowser.resultCount() == 1 && bass.presetName() == beforeFavorite, "Stars collect sounds without loading a different preset");
    }
    {
        BassPresetBrowser restored(bass, preferences);
        restored.setBounds(editor->getLocalBounds()); restored.open();
        button(restored, "Favorites")->onClick();
        check(restored.resultCount() == 1, "Favorites survive closing and reopening the browser");
    }
    std::printf("Preset browser failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
