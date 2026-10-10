#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/KitMap.h"
#include "../Core/SoundPrint.h"
#include "LiveHost.h"
#include "ScreenTools.h"
#include <map>
#include <set>

/** Find drum plugins, load one, map its notes to zones, audition a zone and record the kit. */
class MainComponent : public juce::Component,
                      private juce::TableListBoxModel,
                      private juce::ChangeListener,
                      private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    enum Column { colOn = 1, colPiece, colZone, colNote, colCc4, colHits, colName, colMimicName, colLibrary, colImage };

    void scanPlugins();
    void refreshPluginChoices();
    static juce::String pluginLabel (const juce::PluginDescription&);
    void loadSelectedPlugin();
    void showPluginWindow();
    void useMap (emk::KitMap);
    void saveMap();
    void auditionSelectedZone();
    void recordKit();
    void findNotes();
    void addZone();
    void removeZone();
    void loadMapFile();
    void saveMapFile();
    void enableMidiInputs();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void learnNote (int note);
    void startLearning();
    void learnFromSound (const juce::AudioBuffer<float>& hit, double rate);
    void readNoteNames();
    void convertWavFolder();
    void buildMimicKit();
    void buildMimicKitWith (const juce::Array<juce::File>& instruments, const juce::File& templateKit);
    juce::File outputFolder() const;
    void chooseOutputFolder();
    void showOutputFolder();
    void savePluginState();
    juce::String buildMimicFiles (const juce::Array<juce::File>& inputs, const juce::File& outDir, bool hatsAsRides,
                                  juce::ThreadWithProgressWindow&);
    void setLive (bool on);
    int maxHits() const;
    bool runJob (const juce::String& title, std::function<juce::String (juce::ThreadWithProgressWindow&)> work,
                 std::function<void (const juce::String&)> done);
    void setStatus (const juce::String& text);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::File scanCrashFile() const;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::PropertiesFile::Options settingsOptions;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::DocumentWindow> pluginWindow;
    emk::KitMap kitMap;

    juce::AudioDeviceManager audioDevices;
    juce::AudioSourcePlayer player;
    std::unique_ptr<juce::AudioSource> playing;
    LiveHost live;   // the plugin on the sound card, played from MIDI inputs and its own window
    std::unique_ptr<juce::ThreadWithProgressWindow> job;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> kitDialog;
    std::map<int, emk::SoundPrint> notePrints;   // what each note sounds like, to learn clicks in the plugin window
    juce::uint32 lastPluginNoteMs = 0;
    std::map<juce::String, emk::DrumInfo> drums;       // names and pictures you gave drums, by the plugin's ID for them
    std::map<juce::String, juce::String> libraries;   // library names you gave, by the plugin's library ID
    void loadDrums();
    void saveDrums();
    void rememberDrum (int row);
    void setLibrary (int row, const juce::String&);
    void setImage (int row, const juce::Image&, const juce::File& source = {});
    void offerNameFrom (int row, const juce::File& picture);
    void showImageMenu (int row);
    void startCapture();
    void useCapture (const juce::Image& crop, juce::Point<int> at);
    std::unique_ptr<ScreenTools::CaptureView> captureView;
    std::set<int> soundingNotes;   // from the last "Find notes", to flag rows that play nothing

    juce::TextButton scanButton { "Find plugins" }, loadButton { "Load plugin" },
        showButton { "Show plugin" }, auditionButton { "Listen to zone" }, recordButton { "Record kit" },
        learnButton { "Learn notes" }, findButton { "Find notes" }, addButton { "Add row" },
        removeButton { "Remove row" }, loadMapButton { "Open map..." }, saveMapButton { "Save map..." },
        namesButton { "Read plugin" }, captureButton { "Capture..." },
        convertButton { "Convert WAV folder..." }, kitButton { "Build Mimic kit..." }, saveToButton { "Change..." }, stateButton { "Save state..." };
    juce::Label saveToLabel { {}, "Save to" }, saveToPath;
    juce::ToggleButton fitLimits { "Fit Mimic limits" }, hatAsRide { "Also make hi-hats as 3-zone rides" };
    juce::ComboBox pluginChoice, mapChoice;
    juce::Label kitLabel { {}, "Kit name" }, hitsLabel { {}, "Most hits per zone" };
    juce::TextEditor kitName, hitsPerZone, pluginSearch;
    juce::TableListBox table { {}, this };
    juce::Label status;
    juce::TooltipWindow tooltips { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
