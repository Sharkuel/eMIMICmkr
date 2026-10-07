#include "MainComponent.h"
#include "../Core/BatchRecorder.h"

namespace
{
constexpr double sampleRate = 44100.0;   // the Mimic's rate
constexpr int blockSize = 512;

juce::File outputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("eMIMICmkr");
}

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setVisible (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};

/** A table cell the user can type into. */
class EditableCell : public juce::Label
{
public:
    EditableCell() { setEditable (false, true, false); }
    std::function<void (const juce::String&)> onEdit;
    std::function<void()> onSelect;
    void textWasEdited() override { if (onEdit) onEdit (getText()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSelect) onSelect();   // clicking a cell picks its row, for "Listen to zone"
        Label::mouseDown (e);
    }
};

class RecordJob final : public juce::ThreadWithProgressWindow
{
public:
    RecordJob (juce::AudioPluginInstance& p, const emk::KitMap& m, emk::BatchRecorder::Settings s, juce::File r)
        : ThreadWithProgressWindow ("Recording " + m.kitName, true, true), plugin (p), map (m), settings (s), root (r)
    {
    }

    void run() override
    {
        report = emk::BatchRecorder::record (plugin, map, settings, root, [this] (const auto& p)
        {
            setProgress (p.total > 0 ? (double) p.done / p.total : 0.0);
            setStatusMessage (p.current + "  (" + juce::String (p.done + 1) + " of " + juce::String (p.total) + ")");
            return ! threadShouldExit();
        });
    }

    void threadComplete (bool) override
    {
        if (onDone)
            onDone (report);
    }

    juce::String report;
    std::function<void (const juce::String&)> onDone;

private:
    juce::AudioPluginInstance& plugin;
    emk::KitMap map;
    emk::BatchRecorder::Settings settings;
    juce::File root;
};
} // namespace

MainComponent::MainComponent()
{
    formats.addDefaultFormats();

    settingsOptions.applicationName = "eMIMICmkr";
    settingsOptions.filenameSuffix = ".settings";
    settingsOptions.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions);
    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    audioDevices.initialiseWithDefaultDevices (0, 2);
    audioDevices.addAudioCallback (&player);

    for (auto* b : { &scanButton, &loadButton, &showButton, &auditionButton, &recordButton })
        addAndMakeVisible (b);
    for (juce::Component* c : { (juce::Component*) &pluginChoice, (juce::Component*) &mapChoice,
                                (juce::Component*) &kitLabel, (juce::Component*) &kitName,
                                (juce::Component*) &hitsLabel, (juce::Component*) &hitsPerZone,
                                (juce::Component*) &table, (juce::Component*) &status })
        addAndMakeVisible (c);

    scanButton.onClick = [this] { scanPlugins(); };
    loadButton.onClick = [this] { loadSelectedPlugin(); };
    showButton.onClick = [this] { showPluginWindow(); };
    auditionButton.onClick = [this] { auditionSelectedZone(); };
    recordButton.onClick = [this] { recordKit(); };

    mapChoice.addItem ("Note map: e-drum (Roland TD, hi-hat on CC4)", 1);
    mapChoice.addItem ("Note map: General MIDI", 2);
    mapChoice.setTextWhenNothingSelected ("Note map: your edits");
    mapChoice.onChange = [this]
    {
        if (mapChoice.getSelectedId() == 1) useMap (emk::KitMap::rolandEDrum());
        if (mapChoice.getSelectedId() == 2) useMap (emk::KitMap::generalMidi());
    };

    kitName.onTextChange = [this] { kitMap.kitName = kitName.getText(); saveMap(); };
    hitsPerZone.setInputRestrictions (3, "0123456789");
    hitsPerZone.setText (settings->getValue ("hitsPerZone", "64"));
    hitsPerZone.onTextChange = [this] { settings->setValue ("hitsPerZone", hitsPerZone.getText()); };

    auto& header = table.getHeader();
    header.addColumn ("Record", colOn, 60);
    header.addColumn ("Piece", colPiece, 110);
    header.addColumn ("Zone (file label)", colZone, 200);
    header.addColumn ("Note", colNote, 60);
    header.addColumn ("CC4", colCc4, 60);
    table.setRowHeight (24);

    if (auto xml = settings->getXmlValue ("kitMap"))
        useMap (emk::KitMap::fromValueTree (juce::ValueTree::fromXml (*xml)));
    else
        mapChoice.setSelectedId (1);

    refreshPluginChoices();
    setStatus ("Find your drum plugins to start.");
    setSize (720, 640);
}

MainComponent::~MainComponent()
{
    audioDevices.removeAudioCallback (&player);
    player.setSource (nullptr);
    pluginWindow = nullptr;
    plugin = nullptr;
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto row = area.removeFromTop (32);
    scanButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    loadButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    showButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    pluginChoice.setBounds (row);

    area.removeFromTop (12);
    row = area.removeFromTop (28);
    kitLabel.setBounds (row.removeFromLeft (70));
    kitName.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (8);
    mapChoice.setBounds (row);

    status.setBounds (area.removeFromBottom (64));
    area.removeFromBottom (8);
    row = area.removeFromBottom (32);
    auditionButton.setBounds (row.removeFromLeft (130));
    recordButton.setBounds (row.removeFromRight (130));
    row.removeFromRight (8);
    hitsPerZone.setBounds (row.removeFromRight (50));
    hitsLabel.setBounds (row.removeFromRight (100));

    area.removeFromTop (12);
    area.removeFromBottom (8);
    table.setBounds (area);
}

void MainComponent::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

void MainComponent::useMap (emk::KitMap map)
{
    if (map.kitName.isEmpty())
        map.kitName = kitMap.kitName;
    kitMap = std::move (map);
    kitName.setText (kitMap.kitName, juce::dontSendNotification);
    table.updateContent();
    table.repaint();
    saveMap();
}

void MainComponent::saveMap()
{
    if (auto xml = kitMap.toValueTree().createXml())
        settings->setValue ("kitMap", xml.get());
}

void MainComponent::refreshPluginChoices()
{
    pluginChoice.clear();
    int id = 1;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            pluginChoice.addItem (d.name + " (" + d.pluginFormatName + ")", id++);
}

void MainComponent::scanPlugins()
{
    auto* list = new juce::PluginListComponent (formats, knownPlugins, {}, settings.get(), true);
    list->setSize (700, 500);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Find plugins";
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    setStatus ("Scan your plugin folders, then close the window and pick a plugin.");
}

void MainComponent::loadSelectedPlugin()
{
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    refreshPluginChoices();

    juce::Array<juce::PluginDescription> instruments;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            instruments.add (d);

    const int index = pluginChoice.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, instruments.size()))
    {
        setStatus ("Pick a plugin first.");
        return;
    }

    pluginWindow = nullptr;
    plugin = nullptr;
    juce::String error;
    plugin = formats.createPluginInstance (instruments[index], sampleRate, blockSize, error);
    if (plugin == nullptr)
    {
        setStatus ("Could not load the plugin: " + error);
        return;
    }
    plugin->prepareToPlay (sampleRate, blockSize);
    if (kitName.isEmpty())
    {
        auto program = plugin->getProgramName (plugin->getCurrentProgram()).trim();
        kitName.setText (program.isNotEmpty() ? program : plugin->getName(), juce::sendNotification);
    }
    setStatus ("Loaded " + plugin->getName() + ". Load a kit in the plugin window, set its MIDI map to match "
               "the note map here, then listen to a zone or record the kit.");
    showPluginWindow();
}

void MainComponent::showPluginWindow()
{
    if (plugin == nullptr)
        return;
    if (pluginWindow == nullptr)
        if (auto* editor = plugin->createEditorIfNeeded())
            pluginWindow = std::make_unique<PluginWindow> (editor, plugin->getName());
    if (pluginWindow != nullptr)
        pluginWindow->setVisible (true);
}

void MainComponent::auditionSelectedZone()
{
    const int row = table.getSelectedRow();
    if (plugin == nullptr || ! juce::isPositiveAndBelow (row, kitMap.zones.size()))
    {
        setStatus (plugin == nullptr ? "Load a plugin first." : "Pick a zone in the table first.");
        return;
    }

    const auto zone = kitMap.zones[row];
    juce::AudioBuffer<float> hit;
    {
        emk::HitRenderer renderer (*plugin, {});
        hit = renderer.renderHit (zone.note, 100, zone.cc4);
    }
    plugin->prepareToPlay (sampleRate, blockSize);

    if (hit.getNumSamples() == 0)
    {
        setStatus ("The plugin stayed silent on note " + juce::String (zone.note) + ".");
        return;
    }

    // Play it back in stereo (the same mono hit on both sides) and keep a copy to inspect.
    auto file = outputFolder().getChildFile ("listen.wav");
    emk::BatchRecorder::writeWav (file, hit, sampleRate);
    juce::AudioBuffer<float> stereo (2, hit.getNumSamples());
    stereo.copyFrom (0, 0, hit, 0, 0, hit.getNumSamples());
    stereo.copyFrom (1, 0, hit, 0, 0, hit.getNumSamples());
    player.setSource (nullptr);
    playing = std::make_unique<juce::MemoryAudioSource> (stereo, true, false);
    player.setSource (playing.get());

    setStatus (zone.piece + " " + zone.label + ", note " + juce::String (zone.note) + " at velocity 100: "
               + juce::String (hit.getNumSamples() / sampleRate, 2) + " s. Saved to " + file.getFullPathName());
}

void MainComponent::recordKit()
{
    if (plugin == nullptr)
    {
        setStatus ("Load a plugin first.");
        return;
    }
    if (kitMap.kitName.trim().isEmpty())
    {
        setStatus ("Give the kit a name first; it names the folders and files.");
        return;
    }

    emk::BatchRecorder::Settings s;
    s.hitsPerZone = juce::jlimit (1, 999, hitsPerZone.getText().getIntValue());
    s.render.sampleRate = sampleRate;
    s.render.blockSize = blockSize;

    if (recordJob != nullptr)
        return;
    player.setSource (nullptr);
    recordButton.setEnabled (false);
    auditionButton.setEnabled (false);
    auto job = std::make_unique<RecordJob> (*plugin, kitMap, s, outputFolder());
    job->onDone = [this] (const juce::String& report)
    {
        plugin->prepareToPlay (sampleRate, blockSize);
        const auto kitFolder = outputFolder().getChildFile (emk::BatchRecorder::safeName (kitMap.kitName));
        setStatus (report + "\nTo make Mimic instruments: python3 tools/mci/wav2mci.py \""
                   + kitFolder.getFullPathName() + "\"");
        recordButton.setEnabled (true);
        auditionButton.setEnabled (true);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->recordJob = nullptr;
        });
    };
    job->launchThread();
    recordJob = std::move (job);
}

int MainComponent::getNumRows()
{
    return kitMap.zones.size();
}

void MainComponent::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    if (selected)
        g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId));
}

void MainComponent::paintCell (juce::Graphics&, int, int, int, int, bool)
{
}

juce::Component* MainComponent::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, kitMap.zones.size()))
    {
        delete existing;
        return nullptr;
    }
    auto& zone = kitMap.zones.getReference (row);

    if (column == colOn)
    {
        auto* toggle = dynamic_cast<juce::ToggleButton*> (existing);
        if (toggle == nullptr)
        {
            delete existing;
            toggle = new juce::ToggleButton();
        }
        toggle->setToggleState (zone.enabled, juce::dontSendNotification);
        toggle->onClick = [this, row, toggle]
        {
            kitMap.zones.getReference (row).enabled = toggle->getToggleState();
            mapChoice.setSelectedId (0, juce::dontSendNotification);
            saveMap();
        };
        return toggle;
    }

    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new EditableCell();
    }
    cell->setText (column == colPiece ? zone.piece
                   : column == colZone ? zone.label
                   : column == colNote ? juce::String (zone.note)
                   : zone.cc4 >= 0 ? juce::String (zone.cc4) : juce::String(),
                   juce::dontSendNotification);
    cell->onEdit = [this, row, column] (const juce::String& text)
    {
        auto& z = kitMap.zones.getReference (row);
        if (column == colPiece) z.piece = text.trim();
        if (column == colZone) z.label = text.trim().toUpperCase();
        if (column == colNote) z.note = juce::jlimit (0, 127, text.getIntValue());
        if (column == colCc4) z.cc4 = text.trim().isEmpty() ? -1 : juce::jlimit (0, 127, text.getIntValue());
        mapChoice.setSelectedId (0, juce::dontSendNotification);
        saveMap();
        table.updateContent();
    };
    cell->onSelect = [this, row] { table.selectRow (row); };
    return cell;
}
