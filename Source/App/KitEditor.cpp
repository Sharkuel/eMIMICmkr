#include "KitEditor.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace
{
namespace MimicKit = emk::MimicKit;

const juce::Colour red (0xffe20000);   // the logo's red, as on Record kit

juce::String decibels (double gain)
{
    if (gain <= 0.0001)
        return "-inf dB";
    const auto db = juce::Decibels::gainToDecibels (gain);
    return (db >= 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String panText (double pan)
{
    const auto amount = juce::roundToInt ((pan - 0.5) * 200.0);
    return amount == 0 ? juce::String ("C") : (amount < 0 ? "L " : "R ") + juce::String (std::abs (amount));
}

/** A horizontal fader showing its value in dB; double-click goes back to `home`. */
void gainSlider (juce::Slider& s, double top, double home)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    s.setRange (0.0, top, 0.001);
    s.setSkewFactorFromMidPoint (home);
    s.setDoubleClickReturnValue (true, home);
    s.textFromValueFunction = [] (double v) { return decibels (v); };
    s.valueFromTextFunction = [] (const juce::String& t)
    {
        return t.containsIgnoreCase ("inf") ? 0.0 : juce::Decibels::decibelsToGain (t.retainCharacters ("-+.0123456789").getDoubleValue());
    };
}

void panSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    s.setRange (0.0, 1.0, 0.005);
    s.setDoubleClickReturnValue (true, 0.5);
    s.textFromValueFunction = [] (double v) { return panText (v); };
    s.valueFromTextFunction = [] (const juce::String& t)
    {
        const auto n = t.retainCharacters ("0123456789").getDoubleValue() / 200.0;
        return t.containsIgnoreCase ("L") ? 0.5 - n : t.containsIgnoreCase ("R") ? 0.5 + n : 0.5;
    };
}

juce::String typeLabel (const juce::String& type)
{
    if (type == "Hat") return "Hi-hat";
    if (type == "Ride") return "Ride / 3-zone";
    if (type == "Cymbal") return "Cymbal";
    if (type == "Perc") return "Percussion";
    return type;
}

/** The SLOTS box draws these groups as a gap between rows of tiles. */
int groupOf (const juce::String& slot)
{
    if (slot.startsWith ("Kick")) return 0;
    if (slot.startsWith ("Snare")) return 1;
    if (slot == "Hi-Hat" || slot.startsWith ("Ride")) return 2;
    if (slot.startsWith ("Tom")) return 3;
    if (slot.startsWith ("Crash")) return 4;
    if (slot.startsWith ("Aux")) return 5;
    return 6;
}
} // namespace

//==============================================================================
class KitEditor::SlotTile : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    std::function<void()> onClick, onDoubleClick;
    std::function<void (const juce::StringArray&)> onFiles;
    MimicKit::Slot slot;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        const auto base = findColour (juce::ResizableWindow::backgroundColourId);
        g.setColour (slot.loaded ? base.brighter (0.12f) : base.darker (0.15f));
        g.fillRect (area);
        if (dragOver)
        {
            g.setColour (red.withAlpha (0.25f));
            g.fillRect (area);
        }
        g.setColour (selected || dragOver ? red : findColour (juce::ComboBox::outlineColourId));
        g.drawRect (area, selected ? 2.0f : 1.0f);

        auto text = getLocalBounds().reduced (8, 3);
        auto top = text.removeFromTop (text.getHeight() / 2);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (slot.name.toUpperCase(), top, juce::Justification::bottomLeft, true);
        if (slot.loaded)
            g.drawText (typeLabel (slot.type), top, juce::Justification::bottomRight, true);
        if (! slot.enabled)
        {
            g.setColour (red.withAlpha (0.9f));
            g.drawText ("OFF", top, juce::Justification::bottomRight, true);
        }

        g.setFont (juce::FontOptions (14.0f));
        g.setColour (slot.loaded ? juce::Colours::white : juce::Colours::white.withAlpha (0.35f));
        g.drawText (slot.loaded ? slot.instrument : juce::String ("Empty: drop a .mci here"), text,
                    juce::Justification::topLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".mci"))
                return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        repaint();
        if (onFiles)
            onFiles (files);
    }

private:
    bool dragOver = false;
};

//==============================================================================
class KitEditor::MicRow : public juce::Component
{
public:
    std::function<void (const MimicKit::Mic&)> onChange;

    MicRow()
    {
        addAndMakeVisible (name);
        addAndMakeVisible (level);
        addAndMakeVisible (pan);
        addAndMakeVisible (mute);
        gainSlider (level, 2.0, 0.5);
        panSlider (pan);
        level.setTooltip ("This mic's level in the kit. Double-click: -6 dB.");
        pan.setTooltip ("This mic's pan. Double-click: centre.");
        mute.setTooltip ("Mutes this mic in the kit.");
        mute.setColour (juce::TextButton::buttonOnColourId, red);
        mute.setClickingTogglesState (true);
        level.onValueChange = [this] { send(); };
        pan.onValueChange = [this] { send(); };
        mute.onClick = [this] { send(); };
    }

    void show (const MimicKit::Mic& m)
    {
        mic = m;
        name.setText (m.name.isEmpty() ? juce::String ("Mic") : m.name, juce::dontSendNotification);
        level.setValue (m.level, juce::dontSendNotification);
        pan.setValue (m.pan, juce::dontSendNotification);
        mute.setToggleState (m.muted, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromLeft (80));
        mute.setBounds (r.removeFromRight (52).reduced (0, 2));
        r.removeFromRight (8);
        pan.setBounds (r.removeFromRight (r.getWidth() * 2 / 5));
        r.removeFromRight (8);
        level.setBounds (r);
    }

private:
    void send()
    {
        mic.level = (float) level.getValue();
        mic.pan = (float) pan.getValue();
        mic.muted = mute.getToggleState();
        if (onChange)
            onChange (mic);
    }

    MimicKit::Mic mic;
    juce::Label name;
    juce::Slider level, pan;
    juce::TextButton mute { "Mute" };
};

//==============================================================================
KitEditor::KitEditor (juce::PropertiesFile& s) : settings (s)
{
    for (auto* c : std::initializer_list<juce::Component*> { &slotsGroup, &slotGroup, &micsGroup, &kitGroup })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &kitName, &newButton, &openButton, &fillButton,
                                                             &saveButton, &saveAsButton, &status, &empty })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &slotTitle, &instrumentLabel, &chooseButton, &emptyButton,
                                                             &enabledButton, &volumeLabel, &volume, &tuneLabel, &tune,
                                                             &noMics, &kitVolumeLabel, &kitVolume, &centreButton })
        addAndMakeVisible (c);

    newButton.onClick = [this] { newKit(); };
    newButton.setTooltip ("Starts an empty kit from one of your own Mimic kits, so its pad settings carry over.");
    openButton.onClick = [this] { chooseKit(); };
    openButton.setTooltip ("Opens a Mimic kit (.kit) to edit it.");
    fillButton.onClick = [this] { fillFromFolder(); };
    fillButton.setTooltip ("Puts every .mci in a folder in a slot of its own, as Build Mimic kit does: kick, snare, "
                           "hi-hat, rides, toms smallest first, crashes, then Aux and Xtra.");
    saveButton.onClick = [this] { save (false); };
    saveButton.setTooltip ("Saves the kit. A new name saves a new kit next to this one.");
    saveButton.setColour (juce::TextButton::buttonColourId, red);
    saveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    saveAsButton.onClick = [this] { save (true); };
    saveAsButton.setTooltip ("Saves the kit in a folder you pick, as <name>.mdp/[U] <name>.kit like the Mimic's own.");
    kitName.setTooltip ("The kit's name on the Mimic: it is the file's name.");
    kitName.onTextChange = [this]
    {
        if (hasKit)
            changed();
    };

    slotTitle.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    instrumentLabel.setFont (juce::FontOptions (15.0f));
    instrumentLabel.setMinimumHorizontalScale (0.6f);
    chooseButton.onClick = [this] { chooseInstrument(); };
    chooseButton.setTooltip ("Picks the .mci to load in this slot. The Mimic finds it by its name and library, "
                             "so copy that instrument to the Mimic too.");
    emptyButton.onClick = [this] { emptySelected(); };
    emptyButton.setTooltip ("Takes the instrument out of this slot.");
    enabledButton.onClick = [this]
    {
        MimicKit::setEnabled (kit, selected, enabledButton.getToggleState());
        changed();
        refresh();
    };
    enabledButton.setTooltip ("A slot that is off doesn't play. Pearl's kits leave Ride 2 off.");

    gainSlider (volume, 3.0, 1.0);
    volume.setTooltip ("The instrument's volume in the kit (iv in the .kit). Double-click: as recorded.");
    volume.onValueChange = [this]
    {
        MimicKit::setChannelValue (kit, selected, "iv", (float) volume.getValue());
        changed();
    };
    tune.setSliderStyle (juce::Slider::LinearHorizontal);
    tune.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    tune.setRange (0.0, 1.0, 0.005);
    tune.setDoubleClickReturnValue (true, 0.5);
    tune.textFromValueFunction = [] (double v)
    {
        const auto n = juce::roundToInt ((v - 0.5) * 200.0);
        return n == 0 ? juce::String ("0") : (n > 0 ? "+" : "") + juce::String (n);
    };
    tune.valueFromTextFunction = [] (const juce::String& t) { return 0.5 + t.getDoubleValue() / 200.0; };
    tune.setTooltip ("The instrument's tuning in the kit (tn in the .kit), -100 to +100 like the Mimic's own knob. "
                     "Double-click: as recorded.");
    tune.onValueChange = [this]
    {
        MimicKit::setChannelValue (kit, selected, "tn", (float) tune.getValue());
        changed();
    };

    gainSlider (kitVolume, 1.5, 1.0);
    kitVolume.setTooltip ("The whole kit's volume (KitVol). Double-click: 0 dB.");
    kitVolume.onValueChange = [this]
    {
        kit.set ("KitVol", emk::mci::Value::ofFloat (kitVolume.getValue()));
        changed();
    };
    centreButton.onClick = [this]
    {
        const auto n = MimicKit::centreAll (kit);
        if (n > 0)
            changed();
        refresh();
        setStatus (n == 0 ? "Every mic was already centred." : "Centred " + juce::String (n) + " mics.");
    };
    centreButton.setTooltip ("Pans every mic of every slot to the centre, for mono or centred kits.");

    noMics.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    empty.setJustificationType (juce::Justification::centred);
    empty.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    status.setMinimumHorizontalScale (0.6f);

    setSize (1100, 740);
    refresh();
}

KitEditor::~KitEditor() = default;

void KitEditor::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));
}

void KitEditor::resized()
{
    constexpr int gap = 10, frameTop = 20, frameSide = 8, buttonH = 27;
    auto area = getLocalBounds().reduced (gap);

    auto row = area.removeFromTop (32);
    nameLabel.setBounds (row.removeFromLeft (70));
    kitName.setBounds (row.removeFromLeft (260).reduced (0, 2));
    row.removeFromLeft (gap);
    for (auto* b : { &saveAsButton, &saveButton })
    {
        b->setBounds (row.removeFromRight (100));
        row.removeFromRight (gap);
    }
    row.removeFromRight (gap);
    fillButton.setBounds (row.removeFromRight (160));
    row.removeFromRight (gap);
    openButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (gap);
    newButton.setBounds (row.removeFromRight (110));
    area.removeFromTop (gap);

    status.setBounds (area.removeFromBottom (24));
    area.removeFromBottom (gap);

    // Left: the slots, two columns in the Mimic's order with a little gap between kinds.
    auto left = area.removeFromLeft (area.getWidth() * 55 / 100);
    area.removeFromLeft (gap);
    slotsGroup.setBounds (left);
    empty.setBounds (left);
    auto inside = left.withTrimmedTop (frameTop).reduced (frameSide);
    const int perColumn = (tiles.size() + 1) / 2;
    if (perColumn > 0)
    {
        const int columnW = (inside.getWidth() - gap) / 2;
        // Each column: its tiles plus a 6 px gap at each change of kind.
        for (int c = 0; c < 2; ++c)
        {
            const int first = c * perColumn, last = std::min (tiles.size(), first + perColumn);
            int breaks = 0;
            for (int i = first + 1; i < last; ++i)
                breaks += groupOf (tiles[i]->slot.name) != groupOf (tiles[i - 1]->slot.name);
            const int tileH = std::min (48, (inside.getHeight() - breaks * 6) / std::max (1, perColumn));
            auto column = inside.withX (inside.getX() + c * (columnW + gap)).withWidth (columnW);
            for (int i = first; i < last; ++i)
            {
                if (i > first && groupOf (tiles[i]->slot.name) != groupOf (tiles[i - 1]->slot.name))
                    column.removeFromTop (6);
                tiles[i]->setBounds (column.removeFromTop (tileH).reduced (0, 1));
            }
        }
    }

    // Right: the selected slot, its mics, then the kit-wide settings.
    auto kitBox = area.removeFromBottom (frameTop + 2 * buttonH + 2 * frameSide);
    area.removeFromBottom (gap);
    auto slotBox = area.removeFromTop (frameTop + 5 * 32 + frameSide);
    area.removeFromTop (gap);

    slotGroup.setBounds (slotBox);
    inside = slotBox.withTrimmedTop (frameTop).reduced (frameSide + 4, 0);
    row = inside.removeFromTop (32);
    enabledButton.setBounds (row.removeFromRight (90));
    slotTitle.setBounds (row);
    instrumentLabel.setBounds (inside.removeFromTop (32));
    row = inside.removeFromTop (32).reduced (0, 2);
    chooseButton.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (gap);
    emptyButton.setBounds (row.removeFromLeft (110));
    row = inside.removeFromTop (32);
    volumeLabel.setBounds (row.removeFromLeft (80));
    volume.setBounds (row);
    row = inside.removeFromTop (32);
    tuneLabel.setBounds (row.removeFromLeft (80));
    tune.setBounds (row);

    micsGroup.setBounds (area);
    inside = area.withTrimmedTop (frameTop).reduced (frameSide + 4, frameSide);
    noMics.setBounds (inside.removeFromTop (32));
    for (auto* r : micRows)
        r->setBounds (inside.removeFromTop (34).reduced (0, 2));

    kitGroup.setBounds (kitBox);
    inside = kitBox.withTrimmedTop (frameTop).reduced (frameSide + 4, frameSide);
    row = inside.removeFromTop (buttonH);
    kitVolumeLabel.setBounds (row.removeFromLeft (80));
    kitVolume.setBounds (row);
    inside.removeFromTop (4);
    centreButton.setBounds (inside.removeFromTop (buttonH).removeFromLeft (150));
}

//==============================================================================
juce::File KitEditor::startFolder() const
{
    if (file != juce::File())
        return MimicKit::kitFolderOf (file);
    const juce::File remembered (settings.getValue ("kitEditorFolder"));
    if (remembered.isDirectory())
        return remembered;
    const juce::File templ (settings.getValue ("kitTemplate"));
    return templ.existsAsFile() ? templ.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void KitEditor::chooseKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a Mimic kit", startFolder(), "*.kit;*.KIT");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            openKit (fc.getResult());
    });
}

bool KitEditor::openKit (const juce::File& f)
{
    try
    {
        auto read = emk::mci::readSettings (f);
        if (MimicKit::slotsOf (read).isEmpty())
            throw emk::mci::Error ("it has no kit slots");
        kit = std::move (read);
        hasKit = true;
        file = f;
        settings.setValue ("kitEditorFolder", MimicKit::kitFolderOf (f).getFullPathName());
        kitName.setText (MimicKit::kitName (f), juce::dontSendNotification);
        dirty = false;
        selected = {};
        refresh();
        setStatus ("Opened " + f.getFullPathName());
        return true;
    }
    catch (const std::exception& e)
    {
        setStatus ("Couldn't open " + f.getFileName() + ": " + juce::String (e.what()));
        return false;
    }
}

void KitEditor::newKit()
{
    const juce::File remembered (settings.getValue ("kitTemplate"));
    chooser = std::make_unique<juce::FileChooser> ("Pick one of your Mimic kits to start from: its pad settings carry over",
                                                   remembered.existsAsFile() ? remembered : startFolder(), "*.kit;*.KIT");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            newKitFrom (fc.getResult());
    });
}

void KitEditor::newKitFrom (const juce::File& templateKit)
{
    try
    {
        kit = MimicKit::emptied (emk::mci::readSettings (templateKit));
        settings.setValue ("kitTemplate", templateKit.getFullPathName());
        hasKit = true;
        file = juce::File();
        kitName.setText ("New kit", juce::dontSendNotification);
        selected = {};
        changed();
        refresh();
        setStatus ("A new kit with the pad settings of " + templateKit.getFileName() + ". Drop .mci files on its slots.");
    }
    catch (const std::exception& e)
    {
        setStatus ("Couldn't start from " + templateKit.getFileName() + ": " + juce::String (e.what()));
    }
}

void KitEditor::fillFromFolder()
{
    if (! hasKit)
        return setStatus ("Open a kit or start a new one first.");
    const juce::File last (settings.getValue ("mimicFolder"));
    chooser = std::make_unique<juce::FileChooser> ("Pick the folder of .mci files (or the files) for the kit",
                                                   last.isDirectory() ? last : startFolder(), "*.mci");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        juce::Array<juce::File> files;
        for (const auto& f : fc.getResults())
        {
            if (f.isDirectory())
                files.addArray (f.findChildFiles (juce::File::findFiles, false, "*.mci;*.MCI"));
            else if (f.existsAsFile())
                files.add (f);
        }
        if (files.isEmpty())
            return setStatus (fc.getResults().isEmpty() ? juce::String() : juce::String ("No .mci files there."));
        settings.setValue ("mimicFolder", files[0].getParentDirectory().getFullPathName());
        try
        {
            std::vector<MimicKit::Instrument> instruments;
            for (const auto& f : files)
                instruments.push_back (MimicKit::readInstrument (f));
            std::stable_sort (instruments.begin(), instruments.end(), [] (const auto& a, const auto& b)
                              { return a.file.getFileName() < b.file.getFileName(); });
            MimicKit::Report report;
            kit = MimicKit::build (kit, instruments, report);
            juce::String text = "Placed " + juce::String ((int) report.placed.size()) + " instruments.";
            if (! report.leftOut.isEmpty())
                text << " No slot for: " << report.leftOut.joinIntoString (", ") << ".";
            for (const auto& note : report.notes)
                text << " " << note;
            changed();
            refresh();
            setStatus (text);
        }
        catch (const std::exception& e)
        {
            setStatus ("Couldn't fill the kit: " + juce::String (e.what()));
        }
    });
}

void KitEditor::save (bool askWhere)
{
    if (! hasKit)
        return setStatus ("Open a kit or start a new one first.");
    const auto name = kitName.getText().trim();
    if (name.isEmpty())
        return setStatus ("Give the kit a name first.");

    if (! askWhere && file != juce::File())
    {
        // The same name writes over the kit; a new one saves a new kit beside it.
        if (name == MimicKit::kitName (file))
            return writeTo (file);
        return writeTo (MimicKit::kitFile (MimicKit::kitFolderOf (file), name));
    }
    chooser = std::make_unique<juce::FileChooser> ("Pick the folder to save the kit in (it goes in " + name + ".mdp)",
                                                   startFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, name] (const juce::FileChooser& fc)
    {
        if (fc.getResult().isDirectory())
            writeTo (MimicKit::kitFile (fc.getResult(), name));
    });
}

void KitEditor::writeTo (const juce::File& out)
{
    if (! out.getParentDirectory().createDirectory())
        return setStatus ("Couldn't make the folder " + out.getParentDirectory().getFullPathName());
    const auto result = emk::mci::writeKitFile (kit, out);
    if (result.failed())
        return setStatus ("Couldn't save the kit: " + result.getErrorMessage());
    file = out;
    settings.setValue ("kitEditorFolder", MimicKit::kitFolderOf (out).getFullPathName());
    dirty = false;
    if (onTitleChange)
        onTitleChange (kitName.getText());
    setStatus ("Saved " + out.getFullPathName());
}

//==============================================================================
void KitEditor::chooseInstrument()
{
    if (selected.isEmpty())
        return;
    const juce::File last (settings.getValue ("mimicFolder"));
    chooser = std::make_unique<juce::FileChooser> ("Pick the instrument (.mci) for " + selected,
                                                   last.isDirectory() ? last : startFolder(), "*.mci;*.MCI");
    const auto slot = selected;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            placeFiles (slot, { fc.getResult().getFullPathName() });
    });
}

void KitEditor::placeFiles (const juce::String& slot, const juce::StringArray& files)
{
    select (slot);
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (! f.hasFileExtension ("mci"))
            continue;
        try
        {
            const auto in = MimicKit::readInstrument (f);
            juce::String why;
            if (! MimicKit::canTake (slot, in, why))
                return setStatus (in.name() + " can't go in " + slot + ". " + why);
            MimicKit::place (kit, slot, in);
            settings.setValue ("mimicFolder", f.getParentDirectory().getFullPathName());
            changed();
            refresh();
            juce::String text = slot + " now loads " + in.name();
            // The Mimic finds instruments by name and library, so a twin would load the same sound.
            for (const auto& s : MimicKit::slotList (kit))
                if (s.name != slot && s.loaded && s.instrument == in.name() && s.library == in.settings.getString ("INST0libnam"))
                    text << " (" << s.name << " loads it too)";
            setStatus (text + ".");
        }
        catch (const std::exception& e)
        {
            setStatus ("Couldn't use " + f.getFileName() + ": " + juce::String (e.what()));
        }
        return;   // one instrument per slot
    }
}

void KitEditor::emptySelected()
{
    if (selected.isEmpty())
        return;
    MimicKit::empty (kit, selected);
    changed();
    refresh();
    setStatus (selected + " is empty.");
}

void KitEditor::select (const juce::String& slot)
{
    selected = slot;
    for (auto* t : tiles)
    {
        t->selected = t->slot.name == slot;
        t->repaint();
    }
    showSlot();
}

void KitEditor::refresh()
{
    const auto slots = hasKit ? MimicKit::slotList (kit) : std::vector<MimicKit::Slot>();
    if (tiles.size() != (int) slots.size())
    {
        tiles.clear();
        for (size_t i = 0; i < slots.size(); ++i)
        {
            auto* t = tiles.add (new SlotTile());
            addAndMakeVisible (t);
            t->onClick = [this, t] { select (t->slot.name); };
            t->onDoubleClick = [this, t] { select (t->slot.name); chooseInstrument(); };
            t->onFiles = [this, t] (const juce::StringArray& files) { placeFiles (t->slot.name, files); };
        }
    }
    for (size_t i = 0; i < slots.size(); ++i)
    {
        tiles[(int) i]->slot = slots[i];
        tiles[(int) i]->selected = slots[i].name == selected;
        tiles[(int) i]->repaint();
    }
    if (hasKit && selected.isEmpty() && ! slots.empty())
        selected = slots.front().name;

    empty.setVisible (! hasKit);
    for (auto* b : std::initializer_list<juce::Component*> { &fillButton, &saveButton, &saveAsButton, &kitName,
                                                             &kitVolume, &centreButton })
        b->setEnabled (hasKit);
    kitVolume.setValue (kit.getFloat ("KitVol", 1.0f), juce::dontSendNotification);
    showSlot();
    resized();
}

void KitEditor::showSlot()
{
    MimicKit::Slot slot;
    for (const auto& s : hasKit ? MimicKit::slotList (kit) : std::vector<MimicKit::Slot>())
        if (s.name == selected)
            slot = s;
    const bool any = slot.name.isNotEmpty();

    slotTitle.setText (any ? slot.name : juce::String ("No slot picked"), juce::dontSendNotification);
    instrumentLabel.setText (! any ? juce::String()
                             : slot.loaded ? slot.instrument + (slot.library.isNotEmpty() ? "  (" + slot.library + ")" : juce::String())
                                                 + "  " + typeLabel (slot.type)
                                           : juce::String ("Empty. Drop a .mci on the slot, or choose one."),
                             juce::dontSendNotification);
    chooseButton.setEnabled (any);
    emptyButton.setEnabled (slot.loaded);
    enabledButton.setEnabled (any);
    enabledButton.setToggleState (slot.enabled, juce::dontSendNotification);
    volume.setEnabled (slot.loaded);
    tune.setEnabled (slot.loaded);
    volume.setValue (slot.loaded ? MimicKit::channelValue (kit, slot.name, "iv", 1.0f) : 1.0, juce::dontSendNotification);
    tune.setValue (slot.loaded ? MimicKit::channelValue (kit, slot.name, "tn", 0.5f) : 0.5, juce::dontSendNotification);

    const auto mics = any ? MimicKit::mics (kit, slot.name) : std::vector<MimicKit::Mic>();
    while (micRows.size() > (int) mics.size())
        micRows.removeLast();
    while (micRows.size() < (int) mics.size())
        addAndMakeVisible (micRows.add (new MicRow()));
    for (int m = 0; m < micRows.size(); ++m)
    {
        micRows[m]->show (mics[(size_t) m]);
        micRows[m]->onChange = [this, name = slot.name, m] (const MimicKit::Mic& mic)
        {
            MimicKit::setMic (kit, name, m, mic);
            changed();
        };
    }
    noMics.setText (mics.empty() ? "Load an instrument in this slot to set its mics."
                                 : "Level, pan and mute of each mic. Double-click a fader to reset it.",
                    juce::dontSendNotification);
    resized();
}

void KitEditor::changed()
{
    if (! dirty && onTitleChange)
        onTitleChange (kitName.getText() + " *");
    dirty = true;
}

void KitEditor::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

//==============================================================================
KitEditorWindow::KitEditorWindow (juce::PropertiesFile& settings, std::function<void()> onClosed)
    : DocumentWindow ("Kit editor", juce::Desktop::getInstance().getDefaultLookAndFeel()
                                         .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton | DocumentWindow::minimiseButton | DocumentWindow::maximiseButton),
      closed (std::move (onClosed))
{
    setUsingNativeTitleBar (true);
    page = new KitEditor (settings);
    page->onTitleChange = [this] (const juce::String& name) { setName ("Kit editor - " + name); };
    setContentOwned (page, true);
    setResizable (true, true);
    setResizeLimits (900, 640, 4000, 3000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void KitEditorWindow::closeButtonPressed()
{
    if (! page->hasUnsavedChanges())
        return closed();
    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Kit editor")
                       .withMessage ("The kit has changes you haven't saved. Close it anyway?")
                       .withButton ("Close without saving")
                       .withButton ("Keep editing")
                       .withAssociatedComponent (this);
    juce::Component::SafePointer<KitEditorWindow> self (this);
    juce::AlertWindow::showAsync (options, [self] (int button)
    {
        if (self != nullptr && button == 1)
            self->closed();
    });
}
