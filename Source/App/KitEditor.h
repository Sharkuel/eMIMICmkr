#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/MimicKit.h"
#include "IconButton.h"

/** A page to put together a Pearl Mimic kit (.kit): which instrument each of its 26 slots
    loads, and each slot's volume, tune and mic levels and pans. A new kit starts from one of
    the user's own kits, so the pad settings (TPAD...) and buses carry over untouched. */
class KitEditor : public juce::Component
{
public:
    explicit KitEditor (juce::PropertiesFile& settings);
    ~KitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool hasUnsavedChanges() const { return dirty; }
    /** Opens a kit file; false (with the reason in the status line) if it isn't one. */
    bool openKit (const juce::File&);

    std::function<void (const juce::String&)> onTitleChange;   // "Kit editor - <name>"

    class SlotTile;   // one slot in the SLOTS box
    class MicRow;     // one mic of the selected slot

private:
    void newKit();
    void newKitFrom (const juce::File& templateKit);
    void chooseKit();
    void fillFromFolder();
    void save (bool askWhere);
    void writeTo (const juce::File&);
    void chooseInstrument();
    void placeFiles (const juce::String& slot, const juce::StringArray& files);
    void emptySelected();
    void select (const juce::String& slot);
    void refresh();          // tiles and the selected slot's controls, from `kit`
    void showSlot();
    void changed();          // marks unsaved changes
    void setStatus (const juce::String&);
    juce::File startFolder() const;

    juce::PropertiesFile& settings;
    emk::mci::MciFile kit;
    bool hasKit = false, dirty = false;
    juce::File file;           // where the kit was opened from or last saved to
    juce::String selected;     // slot name
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label nameLabel { {}, "Kit name" };
    juce::TextEditor kitName;
    juce::TextButton newButton { "New kit..." }, openButton { "Open kit..." }, saveButton { "Save" },
        saveAsButton { "Save as..." };
    IconButton fillButton { "Fill from folder", IconButton::folder };

    juce::GroupComponent slotsGroup { {}, "SLOTS" }, slotGroup { {}, "SLOT" }, micsGroup { {}, "MICS" },
        kitGroup { {}, "KIT" };
    juce::OwnedArray<SlotTile> tiles;

    juce::Label slotTitle, instrumentLabel;
    juce::TextButton chooseButton { "Choose .mci..." }, emptyButton { "Empty slot" };
    juce::ToggleButton enabledButton { "Slot on" };
    juce::Label volumeLabel { {}, "Volume" }, tuneLabel { {}, "Tune" };
    juce::Slider volume, tune;
    juce::OwnedArray<MicRow> micRows;
    juce::Label noMics { {}, "Load an instrument in this slot to set its mics." };

    juce::Label kitVolumeLabel { {}, "Kit volume" };
    juce::Slider kitVolume;
    juce::TextButton centreButton { "Centre all pans" };

    juce::Label status;
    juce::Label empty { {}, "Open one of your Mimic kits, or start a new kit from one, to edit it here." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitEditor)
};

/** The kit editor in its own window. Closing it with unsaved changes asks first. */
class KitEditorWindow : public juce::DocumentWindow
{
public:
    KitEditorWindow (juce::PropertiesFile& settings, std::function<void()> onClosed);
    void closeButtonPressed() override;
    KitEditor& editor() { return *page; }

private:
    KitEditor* page;
    std::function<void()> closed;
};
