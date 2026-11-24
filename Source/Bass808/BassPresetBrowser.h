#pragma once

#include "Bass808Processor.h"
#include "Bass808Widgets.h"
#include "BassPresetLibrary.h"
#include <algorithm>

namespace bbg
{
// An in-editor browser: no native popup or extra window that can disappear behind a DAW.
class BassPresetBrowser final : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    explicit BassPresetBrowser(Bass808Processor& processor, juce::File preferences = {}) : bass(processor)
    {
        preferencesFile = preferences == juce::File()
            ? bassUserPresetFolder().getChildFile("Browser.xml") : preferences;
        if (const auto xml = juce::XmlDocument::parse(preferencesFile))
            if (xml->hasTagName("BassPresetBrowser"))
                for (const auto* item : xml->getChildIterator())
                    if (item->hasTagName("Favorite")) favorites.addIfNotAlreadyThere(item->getStringAttribute("key"));

        setComponentID("bass-preset-browser");
        search.setName("Search presets");
        search.setTextToShowWhenEmpty("Search sounds, genres or character...", b808::colour::textMuted);
        search.setIndents(12, 8);
        search.onTextChange = [this] { filter(); };
        search.addKeyListener(this);
        addAndMakeVisible(search);
        category.setName("Preset category");
        character.setName("Preset character");
        category.onChange = [this] { filter(); };
        character.onChange = [this] { filter(); };
        addAndMakeVisible(category);
        addAndMakeVisible(character);
        clear.setTooltip("Clear the search and all library filters");
        clear.onClick = [this] { resetFilters(); };
        closeButton.onClick = [this] { dismiss(); };
        load.onClick = [this] { applyRow(list.getSelectedRow(), true); };
        for (auto* button : { &clear, &closeButton, &load }) addAndMakeVisible(*button);
        static const char* labels[] { "All sounds", "Factory", "My presets", "Favorites" };
        for (size_t i = 0; i < banks.size(); ++i)
        {
            auto& button = banks[i];
            button.setButtonText(labels[i]);
            button.setClickingTogglesState(true);
            button.setRadioGroupId(809);
            button.onClick = [this, i]
            {
                bank = static_cast<int>(i);
                if (bank == 2)
                {
                    selectedGenre = {};
                    if (!genreButtons.empty()) genreButtons[0]->setToggleState(true, juce::dontSendNotification);
                }
                filter();
            };
            addAndMakeVisible(button);
        }
        banks[0].setToggleState(true, juce::dontSendNotification);
        list.setModel(this);
        list.setName("Preset results");
        list.setRowHeight(48);
        list.setColour(juce::ListBox::backgroundColourId, b808::colour::deep);
        list.setOutlineThickness(0);
        list.setMultipleSelectionEnabled(false);
        list.addKeyListener(this);
        addAndMakeVisible(list);
        setVisible(false);
    }

    ~BassPresetBrowser() override { list.setModel(nullptr); }
    std::function<void()> onLoaded, onClose;

    void open()
    {
        refresh();
        setVisible(true);
        toFront(false);
        if (isShowing() && getPeer() != nullptr) search.grabKeyboardFocus();
    }

    void dismiss()
    {
        setVisible(false);
        if (onClose) onClose();
    }

    void refresh()
    {
        entries.clear();
        const auto& programs = bass.programs();
        for (size_t i = 0; i < programs.size(); ++i)
        {
            const auto& p = programs[i];
            entries.push_back({ "factory:" + juce::String(static_cast<int>(i)), p.name, p.genre,
                                bassPresetCategory(p.settings), bassPresetCharacters(p.settings, p.name),
                                static_cast<int>(i), {} });
        }
        for (const auto& file : bassUserPresetFiles())
        {
            // Read metadata without decoding an embedded audio sample.
            if (const auto xml = juce::XmlDocument::parse(file); xml != nullptr && xml->hasTagName("HPDG808Preset"))
            {
                const auto tree = juce::ValueTree::fromXml(*xml);
                const auto settings = bassSettingsFromTree(tree.getChildWithName("BassSettings"));
                const auto name = tree.getProperty("name", file.getFileNameWithoutExtension()).toString();
                entries.push_back({ "user:" + file.getFullPathName(), name, "User", bassPresetCategory(settings),
                                    bassPresetCharacters(settings, name), -1, file });
            }
        }
        std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
        {
            return a.name.compareNatural(b.name) < 0;
        });
        const auto oldCategory = category.getText(), oldCharacter = character.getText();
        juce::StringArray categories, characters, genres;
        for (const auto& e : entries)
        {
            categories.addIfNotAlreadyThere(e.category);
            for (const auto& tag : e.characters) characters.addIfNotAlreadyThere(tag);
            if (e.program >= 0) genres.addIfNotAlreadyThere(e.genre);
        }
        categories.sortNatural(); characters.sortNatural(); genres.sortNatural();
        fillFilter(category, "All categories", categories, oldCategory);
        fillFilter(character, "Any character", characters, oldCharacter);
        genreButtons.clear();
        genres.insert(0, "All genres");
        for (const auto& genre : genres)
        {
            auto button = std::make_unique<juce::TextButton>(genre);
            button->setClickingTogglesState(true);
            button->setRadioGroupId(810);
            button->setToggleState(selectedGenre == genre || (selectedGenre.isEmpty() && genre == "All genres"), juce::dontSendNotification);
            button->onClick = [this, genre] { selectedGenre = genre == "All genres" ? juce::String() : genre; filter(); };
            addAndMakeVisible(*button);
            genreButtons.push_back(std::move(button));
        }
        filter();
        resized();
    }

    void step(int delta)
    {
        if (entries.empty()) refresh();
        if (filtered.empty()) return;
        const auto key = currentKey();
        int index = -1;
        for (size_t i = 0; i < filtered.size(); ++i)
            if (entry(static_cast<int>(i)).key == key) index = static_cast<int>(i);
        if (index < 0) index = delta > 0 ? -1 : 0;
        const int count = static_cast<int>(filtered.size());
        const int next = (index + delta + count) % count;
        list.selectRow(next);
        applyRow(next, false);
    }

    int resultCount() const noexcept { return static_cast<int>(filtered.size()); }
    bool keyPressed(const juce::KeyPress& key) override { return handleKey(key); }

    void paint(juce::Graphics& g) override
    {
        using namespace b808;
        g.fillAll(juce::Colours::black.withAlpha(.7f));
        const auto panel = panelBounds();
        g.setColour(colour::panel);
        g.fillRoundedRectangle(panel.toFloat(), 10);
        g.setColour(colour::panelEdge.brighter(.15f));
        g.drawRoundedRectangle(panel.toFloat().reduced(.5f), 10, 1);
        g.setColour(colour::accentGlow);
        g.setFont(juce::FontOptions(22, juce::Font::bold));
        g.drawText("SOUND LIBRARY", panel.getX() + 24, panel.getY() + 16, 330, 28, juce::Justification::centredLeft);
        g.setColour(colour::textMuted);
        g.setFont(juce::FontOptions(12));
        g.drawText("Search by name, genre, category and character.", panel.getX() + 24, panel.getY() + 45, 460, 20, juce::Justification::centredLeft);
        g.drawText("LIBRARY", panel.getX() + 24, panel.getY() + 84, 160, 18, juce::Justification::centredLeft);
        g.drawText("GENRE", panel.getX() + 24, panel.getY() + 248, 160, 18, juce::Justification::centredLeft);
        const int x = list.getX();
        g.drawText("CATEGORY", x, panel.getY() + 130, 160, 16, juce::Justification::centredLeft);
        g.drawText("CHARACTER", x + 194, panel.getY() + 130, 160, 16, juce::Justification::centredLeft);
        g.drawText(juce::String(resultCount()) + " sounds", list.getRight() - 115, panel.getY() + 152, 115, 20, juce::Justification::centredRight);
        if (filtered.empty())
        {
            g.setColour(colour::textBright);
            g.setFont(juce::FontOptions(18, juce::Font::bold));
            g.drawText("No matching sounds", list.getBounds().withHeight(65).translated(0, 50), juce::Justification::centred);
            g.setColour(colour::textMuted);
            g.setFont(juce::FontOptions(13));
            g.drawText("Try another word or clear the filters.", list.getBounds().withHeight(35).translated(0, 110), juce::Justification::centred);
        }
        g.setColour(colour::textMuted);
        g.setFont(juce::FontOptions(11.5f));
        g.drawText(status.isNotEmpty() ? status : "Click to load  /  Enter to load & close  /  Esc to close",
                   x, panel.getBottom() - 42, list.getWidth() - 150, 26, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const auto panel = panelBounds();
        const int x = panel.getX() + 208, right = panel.getRight() - 24, top = panel.getY();
        closeButton.setBounds(panel.getRight() - 72, top + 20, 48, 28);
        search.setBounds(x, top + 82, right - x - 66, 36);
        clear.setBounds(right - 58, top + 82, 58, 36);
        category.setBounds(x, top + 150, 180, 28);
        character.setBounds(x + 194, top + 150, 180, 28);
        for (size_t i = 0; i < banks.size(); ++i)
            banks[i].setBounds(panel.getX() + 24, top + 108 + static_cast<int>(i) * 32, 158, 27);
        for (size_t i = 0; i < genreButtons.size(); ++i)
            genreButtons[i]->setBounds(panel.getX() + 24, top + 274 + static_cast<int>(i) * 28, 158, 24);
        list.setBounds(x, top + 194, right - x, panel.getHeight() - 254);
        load.setBounds(right - 128, panel.getBottom() - 44, 128, 30);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!panelBounds().contains(e.getPosition())) dismiss();
    }

private:
    struct Entry
    {
        juce::String key, name, genre, category;
        juce::StringArray characters;
        int program;
        juce::File file;
    };
    const Entry& entry(int row) const { return entries[filtered[static_cast<size_t>(row)]]; }
    juce::Rectangle<int> panelBounds() const { return getLocalBounds().reduced(40, 30); }
    static void fillFilter(juce::ComboBox& box, const char* all, const juce::StringArray& values, const juce::String& previous)
    {
        box.clear(juce::dontSendNotification);
        box.addItem(all, 1); box.addItemList(values, 2);
        const int index = values.indexOf(previous);
        box.setSelectedId(index < 0 ? 1 : index + 2, juce::dontSendNotification);
    }

    juce::String currentKey() const
    {
        if (!bass.isUserPreset()) return "factory:" + juce::String(bass.getCurrentProgram());
        for (const auto& e : entries)
            if (e.program < 0 && e.name == bass.presetName()) return e.key;
        return {};
    }

    void resetFilters()
    {
        search.clear();
        category.setSelectedId(1, juce::dontSendNotification);
        character.setSelectedId(1, juce::dontSendNotification);
        bank = 0; selectedGenre = {};
        banks[0].setToggleState(true, juce::dontSendNotification);
        if (!genreButtons.empty()) genreButtons[0]->setToggleState(true, juce::dontSendNotification);
        filter();
    }

    void filter()
    {
        const auto key = currentKey();
        auto words = juce::StringArray::fromTokens(search.getText().trim(), " \t", "\"");
        words.removeEmptyStrings();
        filtered.clear();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            if ((bank == 1 && e.program < 0) || (bank == 2 && e.program >= 0)
                || (bank == 3 && !favorites.contains(e.key))) continue;
            if (selectedGenre.isNotEmpty() && e.genre != selectedGenre) continue;
            if (category.getSelectedId() > 1 && e.category != category.getText()) continue;
            if (character.getSelectedId() > 1 && !e.characters.contains(character.getText())) continue;
            const auto text = e.name + " " + e.genre + " " + e.category + " " + e.characters.joinIntoString(" ")
                            + (e.program < 0 ? " user" : " factory");
            if (std::any_of(words.begin(), words.end(), [&](const auto& word) { return !text.containsIgnoreCase(word); })) continue;
            filtered.push_back(i);
        }
        list.deselectAllRows();
        list.updateContent();
        list.setColour(juce::ListBox::backgroundColourId, filtered.empty() ? juce::Colours::transparentBlack : b808::colour::deep);
        for (size_t i = 0; i < filtered.size(); ++i)
            if (entry(static_cast<int>(i)).key == key) list.selectRow(static_cast<int>(i));
        load.setEnabled(!filtered.empty());
        if (list.getSelectedRow() < 0 && !filtered.empty()) list.selectRow(0);
        status = {};
        repaint();
    }

    int getNumRows() override { return resultCount(); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        using namespace b808;
        if (!juce::isPositiveAndBelow(row, resultCount())) return;
        const auto& e = entry(row);
        g.fillAll(selected ? colour::accent.withAlpha(.23f) : (row % 2 ? colour::panel.withAlpha(.25f) : colour::deep));
        if (e.key == currentKey())
        {
            g.setColour(colour::accentGlow);
            g.fillRect(0, 7, 3, height - 14);
        }
        g.setColour(colour::textBright);
        g.setFont(juce::FontOptions(14, juce::Font::bold));
        g.drawText(e.name, 14, 5, width - 60, 21, juce::Justification::centredLeft);
        g.setColour(colour::textMuted);
        g.setFont(juce::FontOptions(11));
        g.drawText(e.genre + "  /  " + e.category + "  /  " + e.characters.joinIntoString(" · "),
                   14, 26, width - 60, 16, juce::Justification::centredLeft);
        juce::Path star;
        star.addStar({ static_cast<float>(width - 25), height * .5f }, 5, 4.5f, 9.5f);
        const bool favorite = favorites.contains(e.key);
        g.setColour(favorite ? colour::accentGlow : colour::textMuted);
        if (favorite) g.fillPath(star); else g.strokePath(star, juce::PathStrokeType(1));
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (!juce::isPositiveAndBelow(row, resultCount())) return;
        if (event.x >= list.getWidth() - list.getVerticalScrollBar().getWidth() - 44)
        {
            const auto key = entry(row).key;
            if (favorites.contains(key)) favorites.removeString(key); else favorites.add(key);
            juce::XmlElement xml("BassPresetBrowser");
            for (const auto& favorite : favorites) xml.createNewChildElement("Favorite")->setAttribute("key", favorite);
            preferencesFile.getParentDirectory().createDirectory();
            const bool saved = xml.writeTo(preferencesFile);
            filter();
            if (!saved) { status = "Could not save favorites"; repaint(); }
        }
        else applyRow(row, false);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.x < list.getWidth() - 60) applyRow(row, true);
    }
    void returnKeyPressed(int row) override { applyRow(row, true); }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return handleKey(key); }
    bool handleKey(const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey) { dismiss(); return true; }
        if (key == juce::KeyPress::returnKey) { applyRow(list.getSelectedRow(), true); return true; }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            if (!filtered.empty()) list.selectRow(juce::jlimit(0, resultCount() - 1, list.getSelectedRow() + (key == juce::KeyPress::upKey ? -1 : 1)));
            return true;
        }
        return false;
    }
    void applyRow(int row, bool closeAfter)
    {
        if (!juce::isPositiveAndBelow(row, resultCount())) return;
        const auto e = entry(row);
        if (e.program >= 0) bass.applyPreset(e.program);
        else if (const auto preset = readBassPreset(e.file)) bass.applyUserPreset(*preset);
        else { status = "Could not read " + e.file.getFileName(); repaint(); return; }
        if (onLoaded) onLoaded();
        list.repaint();
        if (closeAfter) dismiss();
    }

    Bass808Processor& bass;
    juce::File preferencesFile;
    std::vector<Entry> entries;
    std::vector<size_t> filtered;
    juce::StringArray favorites;
    juce::String selectedGenre, status;
    int bank = 0;
    juce::TextEditor search;
    juce::ComboBox category, character;
    juce::TextButton clear { "CLEAR" }, closeButton { "ESC" }, load { "LOAD SOUND" };
    std::array<juce::TextButton, 4> banks;
    std::vector<std::unique_ptr<juce::TextButton>> genreButtons;
    juce::ListBox list;
};
} // namespace bbg
