#include "MainComponent.h"
#include "../Core/BatchRecorder.h"
#include "../Core/WavToMci.h"
#include "../Core/HitRenderer.h"
#include "../Core/MimicImage.h"
#include "../Core/MimicKit.h"
#include "PluginScanner.h"

namespace
{
constexpr double sampleRate = 44100.0;   // the Mimic's rate
constexpr int blockSize = 512;

juce::File defaultOutputFolder()
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

/** A piece's picture in the table: click for a menu, or drop an image file on it. */
class ImageCell : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    std::function<void (const juce::File&)> onFile;
    juce::Image image;

    void paint (juce::Graphics& g) override
    {
        if (image.isValid())
        {
            g.drawImageWithin (image, 2, 1, getWidth() - 4, getHeight() - 2, juce::RectanglePlacement::centred);
            return;
        }
        g.setColour (findColour (juce::Label::textColourId).withAlpha (0.4f));
        g.setFont (12.0f);
        g.drawText ("add...", getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (onFile && ! files.isEmpty())
            onFile (juce::File (files[0]));
    }
};

/** Runs work on a background thread behind a progress window with a Cancel button. */
class Job final : public juce::ThreadWithProgressWindow
{
public:
    Job (const juce::String& title, std::function<juce::String (ThreadWithProgressWindow&)> w,
         std::function<void (const juce::String&)> d)
        : ThreadWithProgressWindow (title, true, true), work (std::move (w)), done (std::move (d))
    {
    }

    void run() override { report = work (*this); }
    void threadComplete (bool) override { done (report); }

private:
    std::function<juce::String (ThreadWithProgressWindow&)> work;
    std::function<void (const juce::String&)> done;
    juce::String report;
};

/** A drum plugin's own kit map, from its state or a preset file: Superior Drummer 3 or BFD3.
    found is false when the data is neither's. */
emk::KitMap kitFromPluginData (const juce::MemoryBlock& data, juce::String& error, bool& found)
{
    found = true;
    if (const auto sd3 = emk::KitMap::sd3TextIn (data); sd3.isNotEmpty())
        return emk::KitMap::fromSd3 (sd3, error);
    if (const auto bfd3 = emk::KitMap::bfd3TextIn (data); bfd3.isNotEmpty())
        return emk::KitMap::fromBfd3 (bfd3, error);
    if (emk::KitMap::isModoState (data))
        return emk::KitMap::fromModo (data, error);
    if (const auto ugritone = emk::KitMap::ugritoneTextIn (data); ugritone.isNotEmpty())
        return emk::KitMap::fromUgritone (ugritone, error);
    if (const auto soundworks = emk::KitMap::soundworksTextIn (data); soundworks.isNotEmpty())
        return emk::KitMap::fromSoundworks (soundworks, error);
    if (const auto addictive = emk::KitMap::addictiveTextIn (data); addictive.isNotEmpty())
        return emk::KitMap::fromAddictive (addictive, error);
    if (const auto ml = emk::KitMap::mlDrumsTextIn (data); ml.isNotEmpty())
        return emk::KitMap::fromMlDrums (ml, error);
    if (const auto text = data.toString(); text.trimStart().startsWith ("%mapping"))
        return emk::KitMap::fromMdlTone (text, error);
    found = false;
    return {};
}
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
    // A plugin that crashed the last scan is named in this file; it goes on the skip list.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins, scanCrashFile());
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    knownPlugins.addChangeListener (this);
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    audioDevices.initialiseWithDefaultDevices (0, 2);
    audioDevices.addAudioCallback (&player);

    for (auto* b : { &scanButton, &loadButton, &showButton, &auditionButton, &recordButton, &learnButton,
                     &findButton, &addButton, &removeButton, &loadMapButton, &saveMapButton, &namesButton, &convertButton, &kitButton })
        addAndMakeVisible (b);
    addAndMakeVisible (fitLimits);
    addAndMakeVisible (hatAsRide);
    hatAsRide.setToggleState (settings->getBoolValue ("hatAsRide", false), juce::dontSendNotification);
    hatAsRide.onClick = [this] { settings->setValue ("hatAsRide", hatAsRide.getToggleState()); };
    addAndMakeVisible (lighter);
    lighter.setToggleState (settings->getBoolValue ("lighterInstruments", false), juce::dontSendNotification);
    lighter.onClick = [this] { settings->setValue ("lighterInstruments", lighter.getToggleState()); };
    lighter.setTooltip ("Builds each instrument with half the Mimic's wave-cell limit, for kits that glitch "
                        "when many instruments are loaded");
    convertButton.onClick = [this] { convertWavFolder(); };
    kitButton.onClick = [this] { buildMimicKit(); };
    kitButton.setTooltip ("Makes a Mimic kit preset (.kit) from .mci files, starting from one of your own kits "
                          "so its pad settings carry over.");
    for (juce::Component* c : { (juce::Component*) &saveToButton, (juce::Component*) &saveToLabel,
                                (juce::Component*) &saveToPath, (juce::Component*) &stateButton })
        addAndMakeVisible (c);
    saveToButton.onClick = [this] { chooseOutputFolder(); };
    saveToPath.setMinimumHorizontalScale (0.5f);
    showOutputFolder();
    stateButton.onClick = [this] { savePluginState(); };
    stateButton.setTooltip ("Saves what the plugin stores about its kit to a file. Send it to us to help teach "
                            "eMIMICmkr to read piece names (brand and model) from this plugin.");
    namesButton.onClick = [this] { readNoteNames(); };
    addAndMakeVisible (captureButton);
    captureButton.onClick = [this] { startCapture(); };
    captureButton.setTooltip ("Takes a screenshot to pick the selected piece's picture and name (read from the "
                              "picture's text) straight from the plugin's window.");
    audioDevices.addAudioCallback (&live);
    live.onPluginNote = [this] (int note)
    {
        lastPluginNoteMs = juce::Time::getMillisecondCounter();
        learnNote (note);
    };
    live.onHeardHit = [this] (const juce::AudioBuffer<float>& hit, double rate) { learnFromSound (hit, rate); };
    enableMidiInputs();   // the e-kit plays the plugin as soon as one is loaded
    fitLimits.setToggleState (settings->getBoolValue ("fitMimicLimits", true), juce::dontSendNotification);
    fitLimits.onClick = [this]
    {
        settings->setValue ("fitMimicLimits", fitLimits.getToggleState());
        table.updateContent();
        table.repaint();
    };
    for (juce::Component* c : { (juce::Component*) &pluginChoice, (juce::Component*) &pluginSearch,
                                (juce::Component*) &mapChoice,
                                (juce::Component*) &kitLabel, (juce::Component*) &kitName,
                                (juce::Component*) &hitsLabel, (juce::Component*) &hitsPerZone,
                                (juce::Component*) &table, (juce::Component*) &status })
        addAndMakeVisible (c);

    scanButton.onClick = [this] { scanPlugins(); };
    pluginSearch.setTextToShowWhenEmpty ("Search plugins", juce::Colours::grey);
    pluginSearch.setJustification (juce::Justification::centredLeft);
    pluginSearch.setTooltip ("Type part of a plugin's name or maker to narrow the list.");
    pluginSearch.onTextChange = [this] { refreshPluginChoices(); };
    pluginSearch.onReturnKey = [this] { pluginChoice.showPopup(); };
    loadButton.onClick = [this] { loadSelectedPlugin(); };
    showButton.onClick = [this] { showPluginWindow(); };
    auditionButton.onClick = [this] { auditionSelectedZone(); };
    recordButton.onClick = [this] { recordKit(); };
    findButton.onClick = [this] { findNotes(); };
    addButton.onClick = [this] { addZone(); };
    removeButton.onClick = [this] { removeZone(); };
    loadMapButton.onClick = [this] { loadMapFile(); };
    saveMapButton.onClick = [this] { saveMapFile(); };
    learnButton.setClickingTogglesState (true);
    // Red while on, so it isn't left learning by mistake.
    learnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
    learnButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    learnButton.onStateChange = [this]
    {
        const auto text = learnButton.getToggleState() ? "Learning..." : "Learn notes";
        if (learnButton.getButtonText() != text)
            learnButton.setButtonText (text);
    };
    learnButton.onClick = [this]
    {
        if (! learnButton.getToggleState())
        {
            live.setListening (false);
            return setStatus ("Stopped learning notes.");
        }
        startLearning();
    };

    mapChoice.addItem ("Note map: e-drum (Roland TD, hi-hat on CC4)", 1);
    mapChoice.addItem ("Note map: General MIDI", 2);
    // Kontakt keeps its libraries' kits encrypted, so their maps are built in: ids from 100.
    const auto kontakt = emk::KitMap::kontaktLibraries();
    for (int i = 0; i < kontakt.size(); ++i)
        mapChoice.addItem ("Note map: " + kontakt[i] + " (Kontakt)", 100 + i);
    mapChoice.setTextWhenNothingSelected ("Note map: your edits");
    mapChoice.onChange = [this, kontakt]
    {
        const int id = mapChoice.getSelectedId();
        if (id == 1) useMap (emk::KitMap::rolandEDrum());
        if (id == 2) useMap (emk::KitMap::generalMidi());
        if (id >= 100)
        {
            auto map = emk::KitMap::kontaktLibrary (kontakt[id - 100]);
            map.applyNames (drums, libraries);
            useMap (std::move (map));
        }
    };

    kitName.onTextChange = [this] { kitMap.kitName = kitName.getText(); saveMap(); };
    hitsPerZone.setInputRestrictions (3, "0123456789");
    hitsPerZone.setText (settings->getValue ("maxHitsPerZone", "127"));
    hitsPerZone.onTextChange = [this]
    {
        settings->setValue ("maxHitsPerZone", hitsPerZone.getText());
        table.updateContent();
        table.repaint();
    };

    auto& header = table.getHeader();
    header.addColumn ("Record", colOn, 60);
    header.addColumn ("Piece", colPiece, 110);
    header.addColumn ("Name on the Mimic", colMimicName, 200);
    header.addColumn ("Library", colLibrary, 140);
    header.addColumn ("Image", colImage, 60);
    header.addColumn ("Zone (file label)", colZone, 200);
    header.addColumn ("Note", colNote, 60);
    header.addColumn ("CC4", colCc4, 50);
    header.addColumn ("Hits", colHits, 50);
    header.addColumn ("Name in plugin", colName, 180);
    table.setRowHeight (24);

    if (auto xml = settings->getXmlValue ("kitMap"))
        useMap (emk::KitMap::fromValueTree (juce::ValueTree::fromXml (*xml)));
    else
        mapChoice.setSelectedId (1);

    loadDrums();
    refreshPluginChoices();
    setStatus ("Find your drum plugins to start.");
    setSize (1180, 720);
}

MainComponent::~MainComponent()
{
    setLive (false);
    knownPlugins.removeChangeListener (this);
    audioDevices.removeMidiInputDeviceCallback ({}, this);
    audioDevices.removeMidiInputDeviceCallback ({}, &live);
    audioDevices.removeAudioCallback (&live);
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
    pluginSearch.setBounds (row.removeFromLeft (180));
    row.removeFromLeft (8);
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
    auditionButton.setBounds (row.removeFromLeft (120));
    recordButton.setBounds (row.removeFromRight (120));
    row.removeFromRight (8);
    hitsPerZone.setBounds (row.removeFromRight (44));
    hitsLabel.setBounds (row.removeFromRight (124));
    fitLimits.setBounds (row.removeFromRight (130));

    area.removeFromBottom (8);
    row = area.removeFromBottom (28);
    saveToButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    saveToPath.setBounds (row.removeFromRight (200));
    saveToLabel.setBounds (row.removeFromRight (60));
    convertButton.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (6);
    kitButton.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (12);
    hatAsRide.setBounds (row.removeFromLeft (250));
    row.removeFromLeft (6);
    lighter.setBounds (row.removeFromLeft (250));

    area.removeFromTop (12);
    row = area.removeFromTop (28);
    for (auto* b : { &learnButton, &findButton, &namesButton, &captureButton, &stateButton, &addButton, &removeButton })
    {
        b->setBounds (row.removeFromLeft (92));
        row.removeFromLeft (6);
    }
    saveMapButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (6);
    loadMapButton.setBounds (row.removeFromRight (90));

    area.removeFromTop (8);
    area.removeFromBottom (8);
    table.setBounds (area);
}

void MainComponent::loadDrums()
{
    drums.clear();
    libraries.clear();
    if (auto xml = settings->getXmlValue ("drumNames"))
        for (auto* drum : xml->getChildIterator())
        {
            auto& info = drums[drum->getStringAttribute ("id")];
            info.name = drum->getStringAttribute ("name");
            info.image.fromBase64Encoding (drum->getStringAttribute ("image"));
        }
    if (auto xml = settings->getXmlValue ("libraryNames"))
        for (auto* library : xml->getChildIterator())
            libraries[library->getStringAttribute ("id")] = library->getStringAttribute ("name");
}

void MainComponent::saveDrums()
{
    juce::XmlElement xml ("DrumNames");
    for (const auto& [id, info] : drums)
        if (info.name.isNotEmpty() || info.image.getSize() > 0)
        {
            auto* drum = xml.createNewChildElement ("Drum");
            drum->setAttribute ("id", id);
            drum->setAttribute ("name", info.name);
            if (info.image.getSize() > 0)
                drum->setAttribute ("image", info.image.toBase64Encoding());
        }
    settings->setValue ("drumNames", &xml);
    juce::XmlElement libs ("LibraryNames");
    for (const auto& [id, name] : libraries)
        if (name.isNotEmpty())
        {
            auto* library = libs.createNewChildElement ("Library");
            library->setAttribute ("id", id);
            library->setAttribute ("name", name);
        }
    settings->setValue ("libraryNames", &libs);
}

void MainComponent::rememberDrum (int row)
{
    // Named or pictured once, a drum comes back that way in every kit that uses it.
    const auto& zone = kitMap.zones[row];
    if (zone.instrument.isEmpty())
        return;
    drums[zone.instrument] = { zone.name, zone.image };
    saveDrums();
}

void MainComponent::setLibrary (int row, const juce::String& name)
{
    // One library name for every piece from the same plugin library (all of them, for a kit
    // with no library IDs), remembered for that library.
    const auto id = kitMap.zones[row].libraryId();
    for (auto& z : kitMap.zones)
        if (z.libraryId() == id)
            z.library = name;
    if (id.isNotEmpty())
    {
        libraries[id] = name;
        saveDrums();
    }
}

void MainComponent::setImage (int row, const juce::Image& picture, const juce::File& source)
{
    if (! juce::isPositiveAndBelow (row, kitMap.zones.size()))
        return setStatus ("Pick a piece in the table first.");
    const auto png = emk::MimicImage::png (picture);
    if (png.getSize() == 0)
        return setStatus ("That isn't a picture eMIMICmkr can read (PNG, JPEG or GIF).");
    kitMap.setPieceImage (row, png);
    rememberDrum (row);
    saveMap();
    table.updateContent();
    table.repaint();
    setStatus (kitMap.zones[row].piece + " has a new picture (" + juce::String (emk::MimicImage::width) + "x"
               + juce::String (emk::MimicImage::height) + ").");
    if (source != juce::File())
        offerNameFrom (row, source);
}

void MainComponent::offerNameFrom (int row, const juce::File& picture)
{
    const auto name = emk::KitMap::nameFromFileName (picture.getFileName());
    if (name.isEmpty() || name == kitMap.zones[row].name)
        return;

    auto apply = [this, row, name]
    {
        if (! juce::isPositiveAndBelow (row, kitMap.zones.size()))
            return;
        kitMap.setPieceName (row, name);
        rememberDrum (row);
        saveMap();
        table.updateContent();
        table.repaint();
        setStatus (kitMap.zones[row].piece + " is now \"" + name + "\" on the Mimic.");
    };
    const auto mode = settings->getValue ("nameFromPicture", "ask");
    if (mode == "always")
        return apply();
    if (mode == "never")
        return;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Name it after the picture?")
                                      .withMessage ("Call " + kitMap.zones[row].piece + " \"" + name
                                                    + "\" on the Mimic, from the picture's file name?")
                                      .withButton ("Use the name")
                                      .withButton ("Always")
                                      .withButton ("No")
                                      .withAssociatedComponent (this),
                                  [this, apply] (int button)
    {
        if (button == 2)
            settings->setValue ("nameFromPicture", "always");
        if (button == 1 || button == 2)
            apply();
    });
}

void MainComponent::showImageMenu (int row)
{
    table.selectRow (row);
    juce::PopupMenu menu;
    menu.addItem (1, "Choose a picture...");
    menu.addItem (2, "Paste a picture");
    menu.addItem (3, "Capture from the screen...");
    menu.addItem (4, "Remove the picture", kitMap.zones[row].image.getSize() > 0);
    menu.addSeparator();
    menu.addItem (5, "Name pieces after their picture files", true, settings->getValue ("nameFromPicture") == "always");
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, row] (int choice)
    {
        if (choice == 1)
        {
            chooser = std::make_unique<juce::FileChooser> ("Picture for " + kitMap.zones[row].piece, juce::File(),
                                                           "*.png;*.jpg;*.jpeg;*.gif");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, row] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    setImage (row, juce::ImageFileFormat::loadFrom (fc.getResult()), fc.getResult());
            });
        }
        if (choice == 2)
        {
            juce::String error;
            juce::File source;
            const auto picture = ScreenTools::clipboardImage (error, &source);
            if (picture.isValid())
                setImage (row, picture, source);
            else
                setStatus (error);
        }
        if (choice == 3)
            startCapture();
        if (choice == 5)
            settings->setValue ("nameFromPicture", settings->getValue ("nameFromPicture") == "always" ? "ask" : "always");
        if (choice == 4)
        {
            kitMap.setPieceImage (row, {});
            rememberDrum (row);
            saveMap();
            table.updateContent();
            table.repaint();
        }
    });
}

void MainComponent::startCapture()
{
    if (captureView != nullptr)
        return;
    // Out of the way while the screenshot is taken, so the plugin's window shows.
    auto* window = getTopLevelComponent();
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (pluginWindow != nullptr && pluginWindow->isVisible()
                                                          ? pluginWindow->getScreenBounds() : window->getScreenBounds());
    const auto area = display != nullptr ? display->totalArea : window->getScreenBounds();
    window->setVisible (false);
    setStatus ("Taking a screenshot...");
    juce::Timer::callAfterDelay (600, [safe = juce::Component::SafePointer<MainComponent> (this), area]
    {
        if (safe == nullptr)
            return;
        juce::String error;
        auto shot = ScreenTools::grabScreen (error);
        safe->getTopLevelComponent()->setVisible (true);
        if (! shot.isValid())
            return safe->setStatus (error);
        juce::Displays::Display where;
        where.totalArea = area;
        safe->captureView = std::make_unique<ScreenTools::CaptureView> (ScreenTools::displayPart (shot, where));
        auto& view = *safe->captureView;
        view.onBox = [safe] (const juce::Image& crop, juce::Point<int> at)
        {
            if (safe != nullptr)
                safe->useCapture (crop, at);
        };
        view.onClose = [safe]
        {
            juce::MessageManager::callAsync ([safe]
            {
                if (safe == nullptr || safe->captureView == nullptr)
                    return;
                juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
                safe->captureView = nullptr;
                safe->getTopLevelComponent()->toFront (true);
            });
        };
        view.setBounds (area);
        view.addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        view.setVisible (true);
        juce::Desktop::getInstance().setKioskModeComponent (&view, false);
        view.toFront (true);
        view.grabKeyboardFocus();
        const int row = safe->table.getSelectedRow();
        if (juce::isPositiveAndBelow (row, safe->kitMap.zones.size()))
            view.setMessage ("Drag a box around " + safe->kitMap.zones[row].piece + "'s picture or name in the plugin, "
                             "then choose what it is. Esc or right-click when done.");
        safe->setStatus ({});
    });
}

void MainComponent::useCapture (const juce::Image& crop, juce::Point<int> at)
{
    // The pieces, each with its first row.
    juce::StringArray pieces;
    juce::Array<int> rows;
    for (int i = 0; i < kitMap.zones.size(); ++i)
        if (! pieces.contains (kitMap.zones[i].piece))
        {
            pieces.add (kitMap.zones[i].piece);
            rows.add (i);
        }
    const int selected = table.getSelectedRow();
    const auto current = juce::isPositiveAndBelow (selected, kitMap.zones.size()) ? kitMap.zones[selected].piece : juce::String();

    juce::PopupMenu menu, pictures, names;
    for (int p = 0; p < pieces.size(); ++p)
    {
        pictures.addItem (1000 + p, pieces[p]);
        names.addItem (2000 + p, pieces[p]);
    }
    if (current.isNotEmpty())
    {
        menu.addItem (1, "Picture of " + current);
        menu.addItem (2, "Name of " + current + " (reads the text)");
    }
    menu.addSubMenu ("Picture of...", pictures);
    menu.addSubMenu ("Name of...", names);
    menu.addItem (3, "Library name for this kit (reads the text)");

    auto* view = captureView.get();
    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (view)
                                                  .withTargetScreenArea ({ at.x, at.y, 1, 1 }),
                        [safe = juce::Component::SafePointer<MainComponent> (this), crop, rows, selected] (int choice)
    {
        if (safe == nullptr || choice == 0 || safe->captureView == nullptr)
            return;
        auto& self = *safe;
        const int row = choice == 1 || choice == 2 || choice == 3 ? selected
                      : choice >= 2000 ? rows[choice - 2000] : rows[choice - 1000];
        if (! juce::isPositiveAndBelow (row, self.kitMap.zones.size()))
            return;
        const auto piece = self.kitMap.zones[row].piece;
        if (choice == 1 || (choice >= 1000 && choice < 2000))
        {
            self.setImage (row, crop);
            self.captureView->setMessage ("Picture of " + piece + " set. Drag another box, or Esc when done.");
            return;
        }
        juce::String error;
        const auto text = ScreenTools::readText (crop, error);
        if (text.isEmpty())
        {
            self.captureView->setMessage (error.isNotEmpty() ? error : "No text found in that box. Try a box around just the name.");
            return;
        }
        if (choice == 3)
        {
            juce::StringArray done;
            for (int i = 0; i < self.kitMap.zones.size(); ++i)
                if (! done.contains (self.kitMap.zones[i].libraryId()))
                {
                    done.add (self.kitMap.zones[i].libraryId());
                    self.setLibrary (i, text);
                }
            self.captureView->setMessage ("Library name set to \"" + text + "\". Fix any misread letters in the table.");
        }
        else
        {
            self.kitMap.setPieceName (row, text);
            self.rememberDrum (row);
            self.captureView->setMessage ("Name of " + piece + " set to \"" + text + "\". Fix any misread letters in the table.");
        }
        self.saveMap();
        self.table.updateContent();
        self.table.repaint();
    });
}

juce::File MainComponent::outputFolder() const
{
    const auto chosen = settings->getValue ("outputFolder");
    return chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen) ? juce::File (chosen) : defaultOutputFolder();
}

void MainComponent::chooseOutputFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where should recordings and Mimic files go?", outputFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (folder == juce::File())
            return;
        if (! folder.createDirectory() || ! folder.hasWriteAccess())
            return setStatus ("Can't write to " + folder.getFullPathName() + ".");
        settings->setValue ("outputFolder", folder.getFullPathName());
        showOutputFolder();
        setStatus ("Kits will be recorded to " + folder.getFullPathName() + "/<kit name>, with their Mimic files in "
                   "its Mimic folder.");
    });
}

void MainComponent::showOutputFolder()
{
    saveToPath.setText (outputFolder().getFullPathName(), juce::dontSendNotification);
    saveToPath.setTooltip (outputFolder().getFullPathName());
}

void MainComponent::savePluginState()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    const auto name = emk::BatchRecorder::safeName (plugin->getName()) + " state.bin";
    chooser = std::make_unique<juce::FileChooser> ("Save the plugin's state", outputFolder().getChildFile (name));
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, state] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        setStatus (file.replaceWithData (state.getData(), state.getSize())
                       ? "Saved " + juce::String ((int) state.getSize()) + " bytes to " + file.getFullPathName() + "."
                       : "Couldn't write " + file.getFullPathName() + ".");
    });
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
    // Rebuilt after every scan step and search keystroke, so keep the user's pick (or the last
    // plugin loaded) selected. Drum plugins come first; anything else is listed below them, in
    // case a plugin doesn't call itself an instrument.
    auto keep = pluginChoice.getText();
    if (keep.isEmpty())
        keep = settings->getValue ("lastPlugin");
    const auto words = juce::StringArray::fromTokens (pluginSearch.getText().toLowerCase(), true);
    auto matches = [&words] (const juce::PluginDescription& d)
    {
        const auto text = (d.name + " " + d.manufacturerName + " " + d.pluginFormatName).toLowerCase();
        for (const auto& w : words)
            if (w.isNotEmpty() && ! text.contains (w))
                return false;
        return true;
    };

    pluginChoice.clear (juce::dontSendNotification);
    int id = 1, shown = 0, lastId = 0;
    const auto types = knownPlugins.getTypes();
    for (const bool instruments : { true, false })
    {
        bool headed = instruments;
        for (const auto& d : types)
            if (d.isInstrument == instruments && matches (d))
            {
                if (! headed)
                {
                    pluginChoice.addSectionHeading ("Other plugins");
                    headed = true;
                }
                const auto name = pluginLabel (d);
                pluginChoice.addItem (name, id);
                if (name == keep)
                    pluginChoice.setSelectedId (id, juce::dontSendNotification);
                lastId = id++;
                ++shown;
            }
    }
    if (shown == 1)
        pluginChoice.setSelectedId (lastId, juce::dontSendNotification);
    pluginChoice.setTextWhenNothingSelected (types.isEmpty() ? "Find plugins first"
                                             : shown == 0  ? "No plugin matches the search"
                                                           : juce::String (shown) + " plugins");
}

juce::String MainComponent::pluginLabel (const juce::PluginDescription& d)
{
    return d.name + " (" + d.pluginFormatName + ")";
}

juce::File MainComponent::scanCrashFile() const
{
    return settings->getFile().getSiblingFile ("eMIMICmkr-scan-crash.txt");
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Saved after every plugin the scan finds, so a crash later in the scan loses nothing.
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    settings->saveIfNeeded();
    refreshPluginChoices();
}

void MainComponent::scanPlugins()
{
    // Each plugin is opened in a child process (OutOfProcessScanner); one that crashes is skipped.
    // The crash file is a second line of defence should the app itself go down mid-scan.
    auto* list = new juce::PluginListComponent (formats, knownPlugins, scanCrashFile(), settings.get(), true);
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
    const auto chosen = pluginChoice.getText();
    juce::PluginDescription desc;
    bool found = false;
    for (const auto& d : knownPlugins.getTypes())   // getTypes() returns a copy: keep our own
        if (pluginLabel (d) == chosen)
        {
            desc = d;
            found = true;
        }
    if (chosen.isEmpty() || ! found)
    {
        setStatus ("Pick a plugin first.");
        return;
    }
    settings->setValue ("lastPlugin", chosen);

    setLive (false);
    pluginWindow = nullptr;
    plugin = nullptr;
    juce::String error;
    plugin = formats.createPluginInstance (desc, sampleRate, blockSize, error);
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
    showPluginWindow();
    setLive (true);
    setStatus ("Loaded " + plugin->getName() + ". Load a kit in the plugin window, set its MIDI map to match "
               "the note map here, then listen to a zone or record the kit.");
}

void MainComponent::showPluginWindow()
{
    if (plugin == nullptr)
        return;
    if (pluginWindow == nullptr)
    {
        // Some plugins (MODO DRUM through yabridge) crash if they're asked to play while their
        // window is being built, and may change their outputs when it opens: pause them for it,
        // and prepare them again afterwards.
        const bool wasLive = live.getProcessor() != nullptr;
        setLive (false);
        if (auto* editor = plugin->createEditorIfNeeded())
            pluginWindow = std::make_unique<PluginWindow> (editor, plugin->getName());
        if (wasLive)
            setLive (true);
    }
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

    setLive (false);
    const auto zone = kitMap.zones[row];
    juce::AudioBuffer<float> hit;
    {
        emk::HitRenderer renderer (*plugin, {});
        hit = renderer.renderHit (zone.note, 100, zone.cc4);
    }
    plugin->prepareToPlay (sampleRate, blockSize);
    setLive (true);

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
    s.hitsPerZone = maxHits();
    s.fitMimicLimits = fitLimits.getToggleState();
    s.render.sampleRate = sampleRate;
    s.render.blockSize = blockSize;

    player.setSource (nullptr);
    const auto map = kitMap;
    runJob ("Recording " + map.kitName, [this, map, s, root = outputFolder()] (juce::ThreadWithProgressWindow& w)
    {
        auto report = emk::BatchRecorder::record (*plugin, map, s, root, [&w] (const auto& p)
        {
            w.setProgress (p.total > 0 ? (double) p.done / p.total : 0.0);
            w.setStatusMessage (p.current + "  (" + juce::String (p.done + 1) + " of " + juce::String (p.total) + ")");
            return ! w.threadShouldExit();
        });
        return report;
    },
    [this, kitFolder = outputFolder().getChildFile (emk::BatchRecorder::safeName (map.kitName)), kitName = map.kitName]
    (const juce::String& report)
    {
        setStatus (report);
        if (! kitFolder.isDirectory())
            return;
        // Offer the Mimic instruments now, next to the recordings; they can always be made later
        // with "Convert WAV folder...".
        const auto mimic = kitFolder.getChildFile ("Mimic");
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Make the Mimic files?")
                                          .withMessage ("The WAV files of " + kitName + " are in\n" + kitFolder.getFullPathName()
                                                        + "\n\nConvert them to .mci files now? Convert puts them in\n"
                                                        + mimic.getFullPathName()
                                                        + "\nor choose another folder, such as a USB stick for the Mimic.")
                                          .withButton ("Convert")
                                          .withButton ("Choose folder...")
                                          .withButton ("Not now")
                                          .withAssociatedComponent (this),
                                      [this, report, kitFolder, mimic, kitName] (int button)
        {
            auto convert = [this, report, kitFolder, kitName] (const juce::File& outDir)
            {
                runJob ("Converting " + kitName, [this, kitFolder, outDir, hats = hatAsRide.getToggleState(),
                                                    light = lighter.getToggleState()] (juce::ThreadWithProgressWindow& w)
                {
                    return buildMimicFiles ({ kitFolder }, outDir, hats, light, w);
                },
                [this, report, outDir] (const juce::String& built)
                {
                    setStatus (report + "\n" + built);
                    if (outDir.isDirectory())
                        outDir.revealToUser();
                });
            };
            if (button == 1)
                return convert (mimic);
            if (button != 2)
                return;
            const auto last = settings->getValue ("mimicFolder");
            chooser = std::make_unique<juce::FileChooser> ("Where should the Mimic files for " + kitName + " go?",
                                                           last.isNotEmpty() && juce::File (last).isDirectory() ? juce::File (last) : kitFolder);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, convert] (const juce::FileChooser& fc)
            {
                const auto folder = fc.getResult();
                if (folder == juce::File())
                    return;
                settings->setValue ("mimicFolder", folder.getFullPathName());
                convert (folder);
            });
        });
    });
}

juce::String MainComponent::buildMimicFiles (const juce::Array<juce::File>& inputs, const juce::File& outDir, bool hatsAsRides, bool light,
                                             juce::ThreadWithProgressWindow& w)
{
    emk::WavToMci::Options options;
    options.hatAsRide = hatsAsRides;
    options.cellsPercent = light ? 50 : 100;
    w.setProgress (0.0);
    w.setStatusMessage ("Building Mimic instruments");
    outDir.createDirectory();
    const auto report = emk::WavToMci::run (inputs, outDir, options,
        [&w] (const juce::String& line) { w.setStatusMessage (line); },
        [&w] (double progress)
        {
            w.setProgress (progress);
            return ! w.threadShouldExit();
        });
    if (! report.foundHits)
        return "No recognisable hits found in " + inputs[0].getFullPathName() + (inputs.size() > 1 ? " and the other folders." : ".");
    juce::String text = "Built " + juce::String (report.written.size()) + " Mimic instruments in "
                        + outDir.getFullPathName() + ".";
    if (report.stopped)
        text << " Stopped before the end.";
    if (! report.failed.isEmpty())
        text << "\nCould not convert: " << report.failed.joinIntoString ("; ") << ".";
    if (! report.skipped.isEmpty())
        text << "\nSkipped " << report.skipped.size() << " files, for example " << report.skipped[0] << ".";
    return text;
}

void MainComponent::convertWavFolder()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Folders...  (a kit, a piece, or several zone folders: Ctrl/Shift-click to pick more than one)");
    menu.addItem (2, "A .zip file...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (convertButton), [this] (int choice)
    {
        if (choice == 0)
            return;
        const bool zip = choice == 2;
        chooser = std::make_unique<juce::FileChooser> (zip ? "Pick a .zip of WAV hits" : "Pick the folders of WAV hits",
                                                       outputFolder(), zip ? "*.zip" : "");
        const int chooserFlags = juce::FileBrowserComponent::openMode
                          | (zip ? juce::FileBrowserComponent::canSelectFiles
                                 : juce::FileBrowserComponent::canSelectDirectories
                                       | juce::FileBrowserComponent::canSelectMultipleItems);
        chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
        {
            juce::Array<juce::File> inputs;
            for (const auto& f : fc.getResults())
                if (f != juce::File())
                    inputs.add (f);
            if (inputs.isEmpty())
                return;

            // The chooser for where the instruments go starts next to what was picked.
            auto base = inputs.size() == 1 && inputs[0].isDirectory() ? inputs[0] : inputs[0].getParentDirectory();
            for (const auto& f : inputs)
                while (! f.isAChildOf (base) && f != base && base.getParentDirectory() != base)
                    base = base.getParentDirectory();
            const auto title = inputs.size() == 1 ? inputs[0].getFileName() : juce::String (inputs.size()) + " folders";

            // Then where the instruments go: any folder, on any disk.
            chooser = std::make_unique<juce::FileChooser> ("Where should the Mimic files for " + title + " go?", base);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, inputs, title] (const juce::FileChooser& out)
            {
                const auto outDir = out.getResult();
                if (outDir == juce::File())
                    return;
                player.setSource (nullptr);
                runJob ("Converting " + title, [this, inputs, outDir, hats = hatAsRide.getToggleState(),
                                                      light = lighter.getToggleState()] (juce::ThreadWithProgressWindow& w)
                {
                    return buildMimicFiles (inputs, outDir, hats, light, w);
                },
                [this] (const juce::String& report) { setStatus (report); });
            });
        });
    });
}

void MainComponent::buildMimicKit()
{
    const auto last = settings->getValue ("mimicFolder");
    chooser = std::make_unique<juce::FileChooser> ("Pick the .mci files for the kit, or the folder holding them",
                                                   last.isNotEmpty() && juce::File (last).isDirectory() ? juce::File (last) : outputFolder(),
                                                   "*.mci");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        juce::Array<juce::File> instruments;
        for (const auto& f : fc.getResults())
        {
            if (f.isDirectory())
                instruments.addArray (f.findChildFiles (juce::File::findFiles, false, "*.mci;*.MCI"));
            else if (f.existsAsFile())
                instruments.add (f);
        }
        if (instruments.isEmpty())
        {
            if (fc.getResults().size() > 0)
                setStatus ("No .mci files there.");
            return;
        }
        settings->setValue ("mimicFolder", instruments[0].getParentDirectory().getFullPathName());

        const juce::File remembered (settings->getValue ("kitTemplate"));
        if (remembered.existsAsFile())
            return buildMimicKitWith (instruments, remembered);

        chooser = std::make_unique<juce::FileChooser> ("Pick one of your Mimic kits (.kit) to start from: its pad settings carry over",
                                                       instruments[0].getParentDirectory(), "*.kit;*.KIT");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, instruments] (const juce::FileChooser& t)
        {
            if (! t.getResult().existsAsFile())
                return;
            settings->setValue ("kitTemplate", t.getResult().getFullPathName());
            buildMimicKitWith (instruments, t.getResult());
        });
    });
}

void MainComponent::buildMimicKitWith (const juce::Array<juce::File>& instruments, const juce::File& templateKit)
{
    // The kit is named after the folder of instruments ("Mimic" is the one Convert makes, inside the kit's folder).
    auto folder = instruments[0].getParentDirectory();
    auto name = folder.getFileName() == "Mimic" ? folder.getParentDirectory().getFileName() : folder.getFileName();

    kitDialog = std::make_unique<juce::AlertWindow> ("Build Mimic kit",
                                                     juce::String (instruments.size()) + " instruments, starting from the kit\n"
                                                         + templateKit.getFileName() + "\n\nThe kit goes in " + folder.getFullPathName(),
                                                     juce::MessageBoxIconType::NoIcon, this);
    kitDialog->addTextEditor ("name", name, "Kit name");
    kitDialog->addButton ("Build", 1, juce::KeyPress (juce::KeyPress::returnKey));
    kitDialog->addButton ("Other kit to start from...", 2);
    kitDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    kitDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, instruments, templateKit, folder] (int button)
    {
        const auto newKit = kitDialog->getTextEditorContents ("name").trim();
        juce::MessageManager::callAsync ([this] { kitDialog = nullptr; });
        if (button == 2)
        {
            settings->removeValue ("kitTemplate");
            chooser = std::make_unique<juce::FileChooser> ("Pick one of your Mimic kits (.kit) to start from: its pad settings carry over",
                                                           templateKit.getParentDirectory(), "*.kit;*.KIT");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, instruments] (const juce::FileChooser& t)
            {
                if (! t.getResult().existsAsFile())
                    return;
                settings->setValue ("kitTemplate", t.getResult().getFullPathName());
                buildMimicKitWith (instruments, t.getResult());
            });
            return;
        }
        if (button != 1 || newKit.isEmpty())
            return;

        const auto out = emk::MimicKit::kitFile (folder, newKit);
        try
        {
            const auto report = emk::MimicKit::buildFile (instruments, templateKit, out);
            juce::String text = "Built the kit " + out.getFullPathName() + ":";
            for (const auto& [slot, instrument] : report.placed)
                text << " " << slot << " = " << instrument << ";";
            if (! report.leftOut.isEmpty())
                text << "\nNo slot for: " << report.leftOut.joinIntoString (", ") << ".";
            for (const auto& note : report.notes)
                text << "\n" << note;
            setStatus (text);
            out.revealToUser();
        }
        catch (const std::exception& e)
        {
            setStatus ("Could not build the kit: " + juce::String (e.what()));
        }
    }));
}

bool MainComponent::runJob (const juce::String& title,
                            std::function<juce::String (juce::ThreadWithProgressWindow&)> work,
                            std::function<void (const juce::String&)> done)
{
    if (job != nullptr)
        return false;
    setLive (false);
    for (auto* b : { &recordButton, &auditionButton, &findButton, &convertButton })
        b->setEnabled (false);
    job = std::make_unique<Job> (title, std::move (work), [this, done] (const juce::String& report)
    {
        if (plugin != nullptr)
            plugin->prepareToPlay (sampleRate, blockSize);
        setLive (true);
        for (auto* b : { &recordButton, &auditionButton, &findButton, &convertButton })
            b->setEnabled (true);
        done (report);
        // The job can't delete itself from inside its own callback.
        juce::Component::SafePointer<MainComponent> self (this);
        juce::MessageManager::callAsync ([safe = self]
        {
            if (safe != nullptr)
                safe->job = nullptr;
        });
    });
    job->launchThread();
    return true;
}

void MainComponent::findNotes()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");

    player.setSource (nullptr);
    emk::HitRenderer::Settings renderSettings;
    auto found = std::make_shared<juce::Array<int>>();
    runJob ("Finding the notes " + plugin->getName() + " plays",
            [this, renderSettings, found] (juce::ThreadWithProgressWindow& w)
    {
        *found = emk::BatchRecorder::findSoundingNotes (*plugin, renderSettings, [&w] (int note)
        {
            w.setProgress (note / 128.0);
            w.setStatusMessage ("Trying note " + juce::String (note) + " of 127");
            return ! w.threadShouldExit();
        });
        return juce::String();
    },
    [this, found] (const juce::String&)
    {
        soundingNotes = std::set<int> (found->begin(), found->end());
        std::set<int> mapped;
        juce::StringArray silent;
        for (const auto& z : kitMap.zones)
        {
            mapped.insert (z.note);
            if (z.enabled && soundingNotes.count (z.note) == 0)
                silent.add (z.label + " (" + juce::String (z.note) + ")");
        }
        int added = 0;
        for (int note : *found)
            if (mapped.count (note) == 0)
            {
                kitMap.zones.add ({ "?", "NOTE " + juce::String (note), note, -1, false });
                ++added;
            }
        mapChoice.setSelectedId (0, juce::dontSendNotification);
        saveMap();
        table.updateContent();
        table.repaint();

        juce::String report = "The plugin plays " + juce::String (found->size()) + " notes.";
        if (added > 0)
            report << " " << added << " of them weren't in the map and were added at the bottom as \"?\": "
                   << "listen to each, then name its piece and zone and tick it.";
        if (! silent.isEmpty())
            report << "\nSilent rows (shown in red): " << silent.joinIntoString (", ") << ".";
        setStatus (report);
    });
}

void MainComponent::addZone()
{
    const int row = table.getSelectedRow();
    auto zone = juce::isPositiveAndBelow (row, kitMap.zones.size()) ? kitMap.zones[row] : emk::KitZone { "Tom 1", "TOM1 CENTER", 0 };
    const int at = row >= 0 ? row + 1 : kitMap.zones.size();
    kitMap.zones.insert (at, zone);
    mapChoice.setSelectedId (0, juce::dontSendNotification);
    saveMap();
    table.updateContent();
    table.selectRow (at);
}

void MainComponent::removeZone()
{
    const int row = table.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, kitMap.zones.size()))
        return setStatus ("Pick a row to remove first.");
    kitMap.zones.remove (row);
    mapChoice.setSelectedId (0, juce::dontSendNotification);
    saveMap();
    table.updateContent();
    table.selectRow (juce::jmin (row, kitMap.zones.size() - 1));
}

void MainComponent::loadMapFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a note map, a Superior Drummer 3 preset, a BFD3 kit or an MDL Tone mapping",
                                                   outputFolder(), "*.csv;*.sd3p;*.bfd3kit;*.nka");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        juce::MemoryBlock data;
        file.loadFileAsData (data);
        bool pluginKit = false;
        auto map = kitFromPluginData (data, error, pluginKit);
        if (! pluginKit)
            map = emk::KitMap::fromCsv (file.loadFileAsString(), error);
        if (pluginKit && error.isEmpty())
        {
            map.applyNames (drums, libraries);
            if (map.kitName.isEmpty())
                map.kitName = kitMap.kitName.isNotEmpty() ? kitMap.kitName
                                                          : file.getFileNameWithoutExtension().replaceCharacter ('_', ' ');
        }
        if (error.isNotEmpty())
            return setStatus ("Could not read " + file.getFileName() + ". " + error);
        mapChoice.setSelectedId (0, juce::dontSendNotification);
        useMap (std::move (map));
        setStatus ("Opened the map " + file.getFileName() + ".");
    });
}

void MainComponent::saveMapFile()
{
    outputFolder().createDirectory();
    const auto name = emk::BatchRecorder::safeName (kitMap.kitName.isNotEmpty() ? kitMap.kitName : juce::String ("note map"));
    chooser = std::make_unique<juce::FileChooser> ("Save the note map", outputFolder().getChildFile (name + ".csv"), "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        file = file.withFileExtension ("csv");
        setStatus (file.replaceWithText (kitMap.toCsv()) ? "Saved the map to " + file.getFullPathName() + "."
                                                         : "Could not write " + file.getFullPathName() + ".");
    });
}

int MainComponent::maxHits() const
{
    return juce::jlimit (1, 999, hitsPerZone.getText().getIntValue());
}

void MainComponent::readNoteNames()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");

    // Superior Drummer 3 and BFD3 keep their whole kit (slots, articulations, their notes) in their state.
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    juce::String sd3Note, bfd3Mapped;
    // Drums loaded into BFD3 slots its MIDI map leaves out get free notes, so they can be recorded.
    int newNotes = 0;
    if (auto everyDrum = emk::KitMap::bfd3WithEveryDrumMapped (state, newNotes); newNotes > 0)
    {
        plugin->setStateInformation (everyDrum.getData(), (int) everyDrum.getSize());
        state.reset();
        plugin->getStateInformation (state);
        bfd3Mapped = "Gave " + juce::String (newNotes) + " notes in BFD3's MIDI map to drums that had none "
                     "(free notes first, then groove notes). ";
    }
    juce::String error;
    bool pluginKit = false;
    if (auto map = kitFromPluginData (state, error, pluginKit); pluginKit)
    {
        if (error.isNotEmpty())
            return setStatus (error);
        map.applyNames (drums, libraries);
        if (map.kitName.isEmpty())
            map.kitName = kitMap.kitName;
        mapChoice.setSelectedId (0, juce::dontSendNotification);
        useMap (std::move (map));
        sd3Note = bfd3Mapped + "Read the kit's map from " + plugin->getName() + ": each instrument and the notes it plays. ";
    }

    // VST3 plugins can name their notes (Cubase shows them in its drum editor); not all do.
    std::map<int, juce::String> names;
    for (int note = 0; note < 128; ++note)
        if (auto name = plugin->getNameForMidiNoteNumber (note, 10); name && name->trim().isNotEmpty())
            names[note] = name->trim();
    if (names.empty())
        return setStatus (sd3Note.isNotEmpty() ? sd3Note + "Check the ticked rows with Listen to zone, then Save map."
                                               : plugin->getName() + " doesn't share its note names. Use Find notes and "
                                                 "Listen to zone instead, then Save map so you only do it once per library.");

    std::set<int> mapped;
    for (auto& z : kitMap.zones)
    {
        mapped.insert (z.note);
        if (auto it = names.find (z.note); it != names.end())
            z.pluginName = it->second;
    }
    int added = 0, guessed = 0;
    for (const auto& [note, name] : names)
        if (mapped.count (note) == 0 && sd3Note.isEmpty())   // SD3's own map already lists every articulation
        {
            auto [piece, label] = emk::KitMap::guessFromName (name);
            const bool known = piece.isNotEmpty();
            kitMap.zones.add ({ known ? piece : juce::String ("?"), known ? label : "NOTE " + juce::String (note),
                                note, -1, false, name });
            ++added;
            guessed += known ? 1 : 0;
        }
    mapChoice.setSelectedId (0, juce::dontSendNotification);
    saveMap();
    table.updateContent();
    table.repaint();
    if (sd3Note.isNotEmpty())
        return setStatus (sd3Note + "The main zones are ticked; the rest are below them, unticked. Check them with "
                          "Listen to zone, then Save map.");
    setStatus (juce::String (names.size()) + " notes have names in " + plugin->getName() + "; they're in the "
               "\"Name in plugin\" column. " + juce::String (added) + " notes weren't in the map and were added "
               "unticked at the bottom (" + juce::String (guessed) + " with a guessed piece and zone): check them, "
               "then tick the ones to record.");
}

void MainComponent::setLive (bool on)
{
    // The plugin runs on the sound card whenever it's loaded, except while it renders offline.
    if (on && plugin != nullptr)
    {
        player.setSource (nullptr);
        live.setProcessor (plugin.get());
        return;
    }
    if (live.getProcessor() != nullptr)
    {
        live.setProcessor (nullptr);
        if (plugin != nullptr)
            plugin->prepareToPlay (sampleRate, blockSize);
    }
}

void MainComponent::enableMidiInputs()
{
    // Every connected MIDI input: an e-kit module, a keyboard or a pad controller.
    audioDevices.removeMidiInputDeviceCallback ({}, this);
    audioDevices.removeMidiInputDeviceCallback ({}, &live);
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        audioDevices.setMidiInputDeviceEnabled (device.identifier, true);
        names.add (device.name);
    }
    audioDevices.addMidiInputDeviceCallback ({}, this);
    audioDevices.addMidiInputDeviceCallback ({}, &live);
    if (names.isEmpty() && learnButton.getToggleState())
        setStatus ("No MIDI input found. Connect your e-kit or keyboard, then click Learn notes again.");
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this),
                                          note = message.getNoteNumber()]
        {
            if (safe != nullptr)
                safe->learnNote (note);
        });
}

void MainComponent::startLearning()
{
    enableMidiInputs();
    if (table.getSelectedRow() < 0)
        table.selectRow (0);
    const juce::String how = "Hit the pad or key for the selected row, or click that drum in the plugin's window; "
                             "the next row is selected after each one. Click Learn notes again to stop.";
    if (plugin == nullptr)
        return setStatus (how);

    // To tell which drum a click in the plugin's window played, hear every note once first.
    // Short renders: the start of each hit is all the comparison needs.
    player.setSource (nullptr);
    auto prints = std::make_shared<std::map<int, emk::SoundPrint>>();
    emk::HitRenderer::Settings quick;
    quick.sampleRate = sampleRate;
    quick.blockSize = blockSize;
    quick.maxSeconds = emk::SoundPrint::seconds + 0.2;
    quick.giveUpSeconds = 0.3;
    quick.silenceSeconds = 0.05;
    const bool started = runJob ("Listening to every note of " + plugin->getName(),
                                 [this, prints, quick] (juce::ThreadWithProgressWindow& w)
    {
        emk::HitRenderer renderer (*plugin, quick);
        for (int note = 0; note < 128 && ! w.threadShouldExit(); ++note)
        {
            w.setProgress (note / 128.0);
            w.setStatusMessage ("Note " + juce::String (note));
            const auto hit = renderer.renderHit (note, 100);
            if (hit.getNumSamples() > 0)
                (*prints)[note] = emk::SoundPrint::of (hit.getReadPointer (0), hit.getNumSamples(), quick.sampleRate);
        }
        return juce::String();
    },
    [this, prints, how] (const juce::String&)
    {
        notePrints = *prints;
        if (! learnButton.getToggleState())
            return;
        live.setListening (true);
        setStatus (how + " (" + juce::String ((int) notePrints.size()) + " notes play a sound.)");
    });
    if (! started)
        learnButton.setToggleState (false, juce::dontSendNotification);
}

void MainComponent::learnFromSound (const juce::AudioBuffer<float>& hit, double rate)
{
    // A plugin that sends its clicks out as MIDI has already said which note it was.
    if (! learnButton.getToggleState() || notePrints.empty()
        || juce::Time::getMillisecondCounter() - lastPluginNoteMs < 1000)
        return;
    const auto heard = emk::SoundPrint::of (hit.getReadPointer (0), hit.getNumSamples(), rate);
    float ratio = 0.0f;
    int note = emk::closestNote (notePrints, heard, &ratio);
    if (note < 0)
        return;

    // Notes that play the same sound tie; keep the row's own note when it is one of them.
    const int row = table.getSelectedRow();
    if (juce::isPositiveAndBelow (row, kitMap.zones.size()))
    {
        const auto current = notePrints.find (kitMap.zones[row].note);
        if (current != notePrints.end()
            && heard.distanceTo (current->second) <= heard.distanceTo (notePrints[note]) * 1.1f)
            note = current->first;
    }
    learnNote (note);
    if (ratio > 0.0f && ratio < 1.15f)
        setStatus (status.getText() + " That was a close call between two notes: check it with Listen to zone.");
}

void MainComponent::learnNote (int note)
{
    const int row = table.getSelectedRow();
    if (! learnButton.getToggleState() || ! juce::isPositiveAndBelow (row, kitMap.zones.size()))
        return;
    auto& zone = kitMap.zones.getReference (row);
    zone.note = note;
    zone.enabled = true;
    mapChoice.setSelectedId (0, juce::dontSendNotification);
    saveMap();
    table.updateContent();
    table.repaint();
    setStatus (zone.piece + " " + zone.label + " is now note " + juce::String (note) + ".");
    if (row + 1 < kitMap.zones.size())
        table.selectRow (row + 1);
}

int MainComponent::getNumRows()
{
    return kitMap.zones.size();
}

void MainComponent::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId));
    else if (! soundingNotes.empty() && juce::isPositiveAndBelow (row, kitMap.zones.size())
             && kitMap.zones[row].enabled && soundingNotes.count (kitMap.zones[row].note) == 0)
        g.fillAll (juce::Colours::red.withAlpha (0.25f));   // this note played nothing in "Find notes"
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
            table.updateContent();   // the other zones of this piece get a new share of hits
        };
        return toggle;
    }

    if (column == colImage)
    {
        auto* picture = dynamic_cast<ImageCell*> (existing);
        if (picture == nullptr)
        {
            delete existing;
            picture = new ImageCell();
        }
        picture->image = emk::MimicImage::load (zone.image);
        picture->repaint();
        picture->setTooltip ("The picture the Mimic shows for " + zone.piece + ". Click to choose, paste or capture one, "
                             "or drop an image file here; it's resized to 94x63.");
        picture->onClick = [this, row] { showImageMenu (row); };
        picture->onFile = [this, row] (const juce::File& f) { setImage (row, juce::ImageFileFormat::loadFrom (f), f); };
        return picture;
    }

    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new EditableCell();
    }
    const int hits = ! zone.enabled ? 0 : kitMap.hitsFor (row, maxHits(), fitLimits.getToggleState());
    // Empty name and library cells show, greyed, what the Mimic will get instead.
    const auto shownName = zone.name.isNotEmpty() ? zone.name
                                                  : emk::BatchRecorder::safeName (kitMap.kitName + " " + zone.piece);
    const auto shownLibrary = zone.library.isNotEmpty() ? zone.library : kitMap.kitName;
    const bool placeholder = (column == colMimicName && zone.name.isEmpty()) || (column == colLibrary && zone.library.isEmpty());
    cell->setColour (juce::Label::textColourId,
                     getLookAndFeel().findColour (juce::Label::textColourId).withAlpha (placeholder ? 0.45f : 1.0f));
    cell->setText (column == colPiece ? zone.piece
                   : column == colMimicName ? shownName
                   : column == colLibrary ? shownLibrary
                   : column == colZone ? zone.label
                   : column == colNote ? juce::String (zone.note)
                   : column == colHits ? (hits > 0 ? juce::String (hits) + (zone.hits > 0 ? " *" : "") : juce::String())
                   : column == colName ? zone.pluginName
                   : zone.cc4 >= 0 ? juce::String (zone.cc4) : juce::String(),
                   juce::dontSendNotification);
    cell->setEditable (false, column != colName, false);   // names come from the plugin
    cell->setTooltip ({});
    if (column == colPiece)
        cell->setTooltip ("Which instrument this row belongs to. Typing changes this row only: type another "
                          "piece's name to move the row into it, or a new name to make it a piece of its own.");
    if (column == colMimicName)
        cell->setTooltip ("The instrument name the Mimic shows. Double-click to type it, or use Capture... to read it "
                          "from the plugin. Superior Drummer drums keep the name in every kit that uses them. "
                          "Mimic kit presets find instruments by this name, so rename before saving presets.");
    if (column == colLibrary)
        cell->setTooltip ("The library name the Mimic shows; it changes every piece from the same plugin library. "
                          "Empty = the kit name.");
    if (column == colHits)
        cell->setTooltip ("Double-click to set this zone's hits; * marks a count you set. "
                          "Clear it to go back to an even share of the piece's wave cells.");
    cell->onEdit = [this, row, column] (const juce::String& text)
    {
        auto& z = kitMap.zones.getReference (row);
        if (column == colPiece)   // only this row: give it another piece's name to move it there
            kitMap.movePiece (row, text.trim());
        if (column == colMimicName)
        {
            kitMap.setPieceName (row, text.trim());
            rememberDrum (row);
        }
        if (column == colLibrary)
            setLibrary (row, text.trim());
        if (column == colZone) z.label = text.trim().toUpperCase();
        if (column == colNote) z.note = juce::jlimit (0, 127, text.getIntValue());
        if (column == colCc4) z.cc4 = text.trim().isEmpty() ? -1 : juce::jlimit (0, 127, text.getIntValue());
        if (column == colHits)
        {
            const int wanted = text.getIntValue();
            const int set = kitMap.setHits (row, juce::jmin (wanted, maxHits()), fitLimits.getToggleState());
            if (wanted > 0 && set < wanted)
                setStatus (z.piece + " " + z.label + " can have " + juce::String (set) + " hits: the Mimic's limit "
                           "for this piece is shared with its other ticked zones (or capped by Most hits per zone).");
        }
        mapChoice.setSelectedId (0, juce::dontSendNotification);
        saveMap();
        table.updateContent();
    };
    cell->onSelect = [this, row] { table.selectRow (row); };
    return cell;
}
