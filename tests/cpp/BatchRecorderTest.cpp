// Records a kit from a stand-in drum plugin, so the batch recorder can be checked
// without a commercial plugin. Usage: eMIMICmkr_tests OUTPUT_FOLDER
// tests/test_batch_recording.py then converts the result with wav2mci.
#include "../../Source/Core/BatchRecorder.h"
#include "../../Source/App/LiveHost.h"
#include "../../Source/Core/SoundPrint.h"
#include "../../Source/Core/WavToMci.h"
#include "../../Source/Core/MimicImage.h"
#include "../../Source/App/ScreenTools.h"

namespace
{
/** Each note-on plays a decaying noise burst, louder with velocity and panned hard left,
    so the test can tell a centred mono mix from a single channel. */
class FakeDrums final : public juce::AudioPluginInstance
{
public:
    FakeDrums() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}

    const juce::String getName() const override { return "Fake Drums"; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); d.isInstrument = true; }
    void prepareToPlay (double rate, int) override { decay = std::exp (-1.0 / (0.3 * rate)); }
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    using AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        int pos = 0;
        auto render = [&] (int end)
        {
            for (; pos < end; ++pos, level *= decay)
                if (level > 1.0e-6)
                    buffer.setSample (0, pos, (float) (level * (random.nextFloat() * 2.0f - 1.0f)));
        };
        for (const auto m : midi)
        {
            render (m.samplePosition);
            const auto msg = m.getMessage();
            if (msg.isNoteOn() && msg.getNoteNumber() >= 20 && msg.getNoteNumber() <= 70)   // the "kit"
                level = msg.getFloatVelocity() * 0.9;
            if (msg.isController() && msg.getControllerNumber() == 4)
                ++ccCount;
        }
        render (buffer.getNumSamples());
        if (clickSilently)   // a click in a window that sends no MIDI: only heard
        {
            level = 0.8;
            clickSilently = false;
            render (buffer.getNumSamples());   // nothing; the next block plays it
        }
        if (clickNote >= 0)   // a click on a drum in the plugin's window: heard, and sent out as MIDI
        {
            midi.addEvent (juce::MidiMessage::noteOn (10, clickNote, (juce::uint8) 100), 0);
            clickNote = -1;
        }
    }

    int ccCount = 0, clickNote = -1;
    bool clickSilently = false;

private:
    double level = 0.0, decay = 0.0;
    juce::Random random { 1 };
};
/** Just enough of a sound card to drive LiveHost. */
class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice() : AudioIODevice ("fake", "fake") {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

/** A Superior Drummer 3 kit (trimmed from a real preset) read into a map, also when wrapped in other bytes. */
bool sd3KitReads()
{
    const auto file = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("data/sd3-kit.txt");
    juce::MemoryBlock wrapped;
    wrapped.append ("VST3\x01\x02junk", 10);
    wrapped.append (file.loadFileAsString().toRawUTF8(), file.loadFileAsString().getNumBytesAsUTF8() + 1);
    juce::String error;
    const auto map = emk::KitMap::fromSd3 (emk::KitMap::sd3TextIn (wrapped), error);
    auto noteOf = [&map] (const juce::String& piece, const juce::String& label, bool* on = nullptr)
    {
        for (const auto& z : map.zones)
            if (z.piece == piece && z.label == label)
            {
                if (on != nullptr) *on = z.enabled;
                return z.note;
            }
        return -1;
    };
    // The same kit as a VST3 plugin's state reaches the app: XML, the plugin's data in base64.
    juce::MemoryBlock inner;
    inner.append ("VstW\0\0\0\x08", 8);
    inner.append (wrapped.getData(), wrapped.getSize());
    const auto xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <VST3PluginState><IComponent>"
                     + inner.toBase64Encoding() + "</IComponent></VST3PluginState>";
    juce::MemoryBlock vst3;
    vst3.append ("VC2!", 4);
    const auto xmlSize = juce::ByteOrder::swapIfBigEndian ((juce::uint32) xml.getNumBytesAsUTF8());
    vst3.append (&xmlSize, 4);
    vst3.append (xml.toRawUTF8(), xml.getNumBytesAsUTF8() + 1);
    const bool vst3Ok = emk::KitMap::sd3TextIn (vst3) == emk::KitMap::sd3TextIn (wrapped);
    if (const auto real = juce::SystemStats::getEnvironmentVariable ("SD3_STATE", {}); real.isNotEmpty())
    {
        juce::MemoryBlock data;
        juce::File (real).loadFileAsData (data);
        juce::String realError;
        const auto realMap = emk::KitMap::fromSd3 (emk::KitMap::sd3TextIn (data), realError);
        std::cout << "real state: " << realMap.zones.size() << " zones " << realError << "\n";
        for (const auto& z : realMap.zones)
            std::cout << "  " << z.piece << " | " << z.label << " | " << z.note << " | " << (z.enabled ? "on" : "off") << " | " << z.instrument << "\n";
    }
    bool rimclickOn = true;
    const bool ok = error.isEmpty() && vst3Ok
                    && noteOf ("Kick", "KICK1") == 36 && noteOf ("Snare", "SNARE1 CENTER") == 38
                    && noteOf ("Snare", "SNARE1 SIDESTICK") == 37 && noteOf ("Snare", "SNARE1 RIMSHOT") == 76
                    && noteOf ("Snare 2", "SNARE2 CENTER") == 67 && noteOf ("Tom 1", "TOM1 CENTER") == 48
                    && noteOf ("Tom 1", "TOM1 RIMSHOT") == 81 && noteOf ("Tom 1", "TOM1 RIMCLICK", &rimclickOn) == 82
                    && ! rimclickOn && noteOf ("Tom 5", "TOM5 CENTER") == 41
                    && noteOf ("Ride 1", "RIDE1 BOW") == 51 && noteOf ("Ride 1", "RIDE1 BELL") == 53
                    && noteOf ("Ride 1", "RIDE1 EDGE") == 59 && noteOf ("Crash 2", "CRASH2 EDGE") == 57
                    && noteOf ("Splash 1", "SPLASH1 EDGE") == 55 && noteOf ("Hi-Hat", "HATS EDGE OPEN2") == 7
                    && noteOf ("Hi-Hat", "HATS BOW CLOSED") == 8 && noteOf ("Hi-Hat", "HATS PEDAL CLOSED") == 21
                    && noteOf ("Hi-Hat", "HATS FOOT SPLASH") == 23 && noteOf ("Kick 2", "KICK2") == 35
                    && noteOf ("Octoban1", "PERC OCTOBAN1 MAIN") == 69 && noteOf ("Octoban1", "PERC OCTOBAN1 SECONDARY") == 4;
    // Names typed for drums come back by their SD3 ID, one piece each.
    auto named = map;
    juce::MemoryBlock png ("\x89PNG\r\n\x1a\nfake", 12);
    std::map<juce::String, emk::DrumInfo> drums { { "SL-DRUMFACTORYARCADES/SD01_01", { "5x14\" DW Timeless Timber Maple", png } },
                                                  { "SL-DRUMFACTORYARCADES/SD01_02", { "5x14\" DW Timeless Timber Maple", {} } } };
    named.applyNames (drums, { { "SL-DRUMFACTORYARCADES", "SD3 Drum Factory Arcades" } });
    auto zoneCalled = [&named] (const juce::String& label) -> const emk::KitZone&
    {
        for (const auto& z : named.zones)
            if (z.label == label)
                return z;
        return named.zones.getReference (0);
    };
    juce::String csvError;
    const auto back = emk::KitMap::fromCsv (named.toCsv(), csvError);
    const bool namesOk = noteOf ("Snare", "SNARE1 CENTER") == 38
                         && zoneCalled ("SNARE1 CENTER").name == "5x14\" DW Timeless Timber Maple"
                         && zoneCalled ("SNARE1 RIMSHOT").image == png
                         && zoneCalled ("SNARE2 CENTER").name == "5x14\" DW Timeless Timber Maple 2"
                         && zoneCalled ("SNARE1 CENTER").piece == "Snare"
                         && zoneCalled ("TOM1 CENTER").library == "SD3 Drum Factory Arcades"
                         && back.toCsv() == named.toCsv() && back.zones[1].name == named.zones[1].name
                         && emk::KitMap::fromValueTree (juce::ValueTree::fromXml (*named.toValueTree().createXml())).toCsv()
                                == named.toCsv();
    juce::String notSd3;
    emk::KitMap::fromSd3 ("piece,zone,note", notSd3);
    std::printf ("sd3 kit %s (%d zones, snare rimshot %d)\n", ok && notSd3.isNotEmpty() ? "ok" : "WRONG",
                 map.zones.size(), noteOf ("Snare", "SNARE1 RIMSHOT"));
    std::printf ("drum names %s\n", namesOk ? "ok" : "WRONG");
    return ok && notSd3.isNotEmpty() && namesOk;
}

/** A BFD3 kit (from a real state, trimmed) read from a VST3 state, and from a kit preset with no MIDI map. */
bool bfd3KitReads()
{
    const auto xml = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("data/bfd3-kit.xml").loadFileAsString();
    // As the plugin's state reaches the app: BFD3's own header, then the XML, in base64 inside JUCE's VST3 wrapper.
    juce::MemoryBlock inner;
    inner.append ("BFDstate\x0c\0\0\0\0\0\0\0\x01\0\0\0", 16);
    inner.append (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
    const auto wrapper = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <VST3PluginState><IComponent>"
                         + inner.toBase64Encoding() + "</IComponent></VST3PluginState>";
    juce::MemoryBlock vst3;
    vst3.append ("VC2!", 4);
    const auto size = juce::ByteOrder::swapIfBigEndian ((juce::uint32) wrapper.getNumBytesAsUTF8());
    vst3.append (&size, 4);
    vst3.append (wrapper.toRawUTF8(), wrapper.getNumBytesAsUTF8() + 1);

    juce::String error, presetError, sd3Error;
    const auto map = emk::KitMap::fromBfd3 (emk::KitMap::bfd3TextIn (vst3), error);
    const auto preset = xml.upToFirstOccurrenceOf ("<BFD2MidiMap", false, false) + "</root>";   // a .bfd3kit has no map
    const auto presetMap = emk::KitMap::fromBfd3 (emk::KitMap::bfd3TextIn (juce::MemoryBlock (preset.toRawUTF8(), preset.getNumBytesAsUTF8())), presetError);
    auto find = [] (const emk::KitMap& m, const juce::String& piece, const juce::String& label) -> const emk::KitZone*
    {
        for (const auto& z : m.zones)
            if (z.piece == piece && z.label == label)
                return &z;
        return nullptr;
    };
    auto noteOf = [&] (const emk::KitMap& m, const juce::String& piece, const juce::String& label, bool on = true)
    {
        const auto* z = find (m, piece, label);
        return z != nullptr && z->enabled == on ? z->note : -1;
    };
    bool ok = error.isEmpty() && presetError.isEmpty() && map.zones.size() == presetMap.zones.size()
              && emk::KitMap::sd3TextIn (vst3).isEmpty() && emk::KitMap::bfd3TextIn (juce::MemoryBlock ("PlugVersion 3", 13)).isEmpty()
              && map.kitName == "BFD Crush - I Want It All";
    for (const auto* m : { &map, &presetMap })
        ok = ok && noteOf (*m, "Kick", "KICK1") == 24 && noteOf (*m, "Snare", "SNARE1 CENTER") == 26
             && noteOf (*m, "Snare", "SNARE1 RIMSHOT") == 29 && noteOf (*m, "Snare", "SNARE1 SIDESTICK") == 25
             && noteOf (*m, "Snare", "SNARE1 RIMCLICK", false) == 88 && noteOf (*m, "Snare", "SNARE1 DRAG", false) == 27
             && noteOf (*m, "Tom 1", "TOM1 CENTER") == 40 && noteOf (*m, "Tom 1", "TOM1 RIMSHOT") == 73
             && noteOf (*m, "Tom 1", "TOM1 RIMCLICK") == 94 && noteOf (*m, "Tom 6", "TOM6 CENTER") == 33
             && noteOf (*m, "Hi-Hat", "HATS BOW CLOSED") == 30 && noteOf (*m, "Hi-Hat", "HATS EDGE OPEN4") == 39
             && noteOf (*m, "Hi-Hat", "HATS BOW OPEN2") == 46 && noteOf (*m, "Hi-Hat", "HATS PEDAL CLOSED") == 32
             && noteOf (*m, "Hi-Hat", "HATS FOOT SPLASH") == 56
             && noteOf (*m, "Ride 1", "RIDE1 BOW") == 60 && noteOf (*m, "Ride 1", "RIDE1 BELL") == 71
             && noteOf (*m, "Ride 1", "RIDE1 EDGE") == 50 && noteOf (*m, "Ride 1", "RIDE1 CHOKE", false) == 87
             && noteOf (*m, "Crash 1", "CRASH1 EDGE") == 41 && noteOf (*m, "Crash 1", "CRASH1 BOW") == 52
             && noteOf (*m, "Crash 1", "CRASH1 BELL", false) == 62 && noteOf (*m, "China 1", "CHINA1 EDGE") == 45
             && noteOf (*m, "Splash 1", "SPLASH1 EDGE") == 48;
    const auto* snare = find (map, "Snare", "SNARE1 CENTER");
    ok = ok && snare != nullptr && snare->name == "Snare AXM Open High" && snare->library == "Crush Drums"
         && snare->instrument == "Crush Drums/Snare AXM Open High";
    if (const auto real = juce::SystemStats::getEnvironmentVariable ("BFD3_STATE", {}); real.isNotEmpty())
    {
        juce::MemoryBlock data;
        juce::File (real).loadFileAsData (data);
        juce::String realError;
        const auto realMap = emk::KitMap::fromBfd3 (emk::KitMap::bfd3TextIn (data), realError);
        std::cout << "real state: " << realMap.zones.size() << " zones " << realError << "\n";
        for (const auto& z : realMap.zones)
            std::cout << "  " << z.piece << " | " << z.label << " | " << z.note << " | " << (z.enabled ? "on" : "off") << " | " << z.pluginName << "\n";
    }
    juce::String empty;
    emk::KitMap::fromBfd3 ("<root><BFD2KitInfo/></root>", empty);
    std::printf ("bfd3 kit %s (%d zones)\n", ok && empty.isNotEmpty() ? "ok" : "WRONG", map.zones.size());
    return ok && empty.isNotEmpty();
}

/** An Ugritone kit, as its plugin state holds it (UGRITONE_STATE names a real state file). */
bool ugritoneKitReads()
{
    juce::String params;
    for (int note : { 36, 48, 47, 45, 43, 41, 38, 42, 44, 46, 32, 49, 50, 57, 58, 55, 56, 59, 52, 61, 51, 53, 54 })
        params << juce::String (note / 127.0, 6) << ",";
    const juce::String text = "<Root curkitpresetname=\"ModernDeathMetal\" "
        "LastLoadedPresetFile=\"/mnt/SL Libraries/Soundbanks/KVLT Drums II Data/Presets/OSDM/ModernDeathMetal.xml\" "
        "Loadedpresetname0panels=\"DM Felt Tight\" PanelName0panels=\"KICK\" "
        "Loadedpresetname1panels=\"DM SPL 12\" PanelName1panels=\"SNARE\" "
        "Loadedpresetname2panels=\"DM Pancake 10i\" PanelName2panels=\"TOM1\" "
        "Loadedpresetname3panels=\"DM B8 Pro\" PanelName3panels=\"HIHAT\" "
        "Loadedpresetname4panels=\"DM Custom Z18\" PanelName4panels=\"CRASH2\" "
        "Loadedpresetname5panels=\"Disabled\" PanelName5panels=\"SPLASH1\" "
        "Loadedpresetname6panels=\"DM 2002 GigaBell\" PanelName6panels=\"RIDE\">"
        "<AllUserParam>" + params + "0,0,0.5</AllUserParam></Root>";

    juce::MemoryBlock state;
    state.append ("VC2!\0\0\0\0", 8);
    state.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    const auto found = emk::KitMap::ugritoneTextIn (state);
    juce::String error;
    const auto m = emk::KitMap::fromUgritone (found, error);
    auto find = [&m] (const char* label) -> const emk::KitZone*
    {
        for (const auto& z : m.zones)
            if (z.label == label)
                return &z;
        return nullptr;
    };
    const auto* kick = find ("KICK1");
    const auto* tom = find ("TOM1 CENTER");
    const auto* open = find ("HATS BOW OPEN4");
    const auto* crash = find ("CRASH2 EDGE");
    const auto* bell = find ("RIDE1 BELL");
    bool ok = error.isEmpty() && m.kitName == "ModernDeathMetal" && kick != nullptr && kick->note == 36
              && kick->name == "DM Felt Tight" && kick->library == "KVLT Drums II"
              && tom != nullptr && tom->note == 48 && find ("SNARE1 CENTER")->note == 38
              && open != nullptr && open->note == 32 && find ("HATS BOW OPEN2")->note == 46
              && find ("CRASH2 CHOKE") != nullptr && find ("CRASH2 CHOKE")->note == 58 && ! find ("CRASH2 CHOKE")->enabled && crash != nullptr && crash->note == 57 && crash->enabled
              && bell != nullptr && bell->note == 53 && find ("SPLASH1 EDGE") == nullptr;

    juce::String notUgritone;
    emk::KitMap::fromUgritone ("<Root/>", notUgritone);
    ok = ok && notUgritone.isNotEmpty();

    if (const auto path = juce::SystemStats::getEnvironmentVariable ("UGRITONE_STATE", {}); path.isNotEmpty())
    {
        juce::MemoryBlock real;
        juce::File (path).loadFileAsData (real);
        juce::String realError;
        const auto r = emk::KitMap::fromUgritone (emk::KitMap::ugritoneTextIn (real), realError);
        std::printf ("Ugritone state: %s, %d zones, error '%s'\n", r.kitName.toRawUTF8(), r.zones.size(), realError.toRawUTF8());
        for (const auto& z : r.zones)
            std::printf ("  %-10s %-20s %3d %s %s\n", z.piece.toRawUTF8(), z.label.toRawUTF8(), z.note, z.enabled ? "on " : "off",
                         z.name.toRawUTF8());
        ok = ok && realError.isEmpty();
    }
    if (! ok)
        std::printf ("ugritoneKitReads failed (%s)\n", error.toRawUTF8());
    return ok;
}

/** A DW Soundworks kit, cut down from one Alexandre saved (DW Maple Mahogany, 2026-10-10). */
bool soundworksKitReads()
{
    auto inst = [] (int mixer, const char* channel, const char* drum, const char* name, int note)
    {
        return juce::String ("<Instrument Mixer=\"") + juce::String (mixer) + "\" Midi=\"" + juce::String (note)
             + "\" LibName=\"" + (juce::String (drum).isEmpty() ? "" : "DW Maple Mahogany") + "\" MixerName=\"" + channel
             + "\" InstrumentName=\"" + drum + "\" Name=\"" + name + "\"><Sound VelIx=\"1\"/></Instrument>\r\n";
    };
    const juce::String snare = "DW TrueCast 6.5x14 Snare Drum", hat = "Zildjian 15'' Sweet K Hi-Hat";
    const auto text = "<PDPreset>\r\n<InstDef Ver=\"213\" InstrumentType=\"1\">\r\n"
        + inst (1, "Kick", "DW Maple Mahogany 16x22 Plastic Beater P3BD Coated", "Center", 36)
        + inst (1, "Instrument 01", "", "ADD 1", -1)
        + inst (2, "Snare", snare.toRawUTF8(), "Center", 38) + inst (2, "Snare", snare.toRawUTF8(), "Edge", -1)
        + inst (2, "Snare", snare.toRawUTF8(), "Rimshot", 40) + inst (2, "Snare", snare.toRawUTF8(), "X Stick", 37)
        + inst (2, "Snare", snare.toRawUTF8(), "Rim", 4) + inst (2, "Snare", snare.toRawUTF8(), "Center SO", 19)
        + inst (3, "Floor 1", "DW Maple Mahogany 12x14 Floor Tom", "Rimshot", 58)
        + inst (3, "Floor 1", "DW Maple Mahogany 12x14 Floor Tom", "Center", 43)
        + inst (8, "Hi Hat", hat.toRawUTF8(), "Open 4 Bow", 46) + inst (8, "Hi Hat", hat.toRawUTF8(), "Normal Closed Edge", 81)
        + inst (8, "Hi Hat", hat.toRawUTF8(), "Tight Closed Bow", 74) + inst (8, "Hi Hat", hat.toRawUTF8(), "Pedal Closed", 44)
        + inst (9, "Ride", "Zildjian 22'' Sweet K Ride-Crash", "Bell", 53)
        + inst (13, "Crash 4", "Zildjian 19'' Sweet K Crash", "Edge", 52) + inst (13, "Crash 4", "Zildjian 19'' Sweet K Crash", "Bell", 96)
        + inst (18, "Stack", "Paiste Signature 8", "Edge", 99)
        + inst (19, "Crash 1", "Sabian Freq 17 Crash", "Edge", 52)
        + inst (26, "Cowbell", "Meinl Black 8", "Edge", 102) + inst (27, "Cowbell", "LP 229 SG", "Edge", 103)
        + inst (20, "Instrument 20", "", "MAIN", 61)
        + "</InstDef>\r\n<InstDef Ver=\"213\" InstrumentType=\"2\"/><Pars Ver=\"213\"><Par ix=\"0\" Name=\"MainVol\" val=\"1\"/></Pars></PDPreset>";

    juce::MemoryBlock state;
    state.append ("\0\0\0\0K\001", 6);
    state.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    juce::String error;
    const auto m = emk::KitMap::fromSoundworks (emk::KitMap::soundworksTextIn (state), error);
    auto find = [&m] (const char* label) -> const emk::KitZone*
    {
        for (const auto& z : m.zones)
            if (z.label == label)
                return &z;
        return nullptr;
    };
    auto is = [&] (const char* label, const char* piece, int note, bool on)
    {
        const auto* z = find (label);
        return z != nullptr && z->piece == piece && z->note == note && z->enabled == on;
    };
    bool ok = error.isEmpty() && m.kitName == "DW Maple Mahogany" && is ("KICK1", "Kick", 36, true)
              && find ("KICK1")->name == "DW Maple Mahogany 16x22 Plastic Beater P3BD Coated" && find ("KICK1")->library == "DW Maple Mahogany"
              && is ("SNARE1 CENTER", "Snare", 38, true) && is ("SNARE1 RIMSHOT", "Snare", 40, true)
              && is ("SNARE1 SIDESTICK", "Snare", 37, true) && is ("SNARE1 RIMCLICK", "Snare", 4, true)
              && find ("SNARE1 EDGE") == nullptr   // no note
              && is ("SNARE2 CENTER", "Snare 2", 19, false) && find ("SNARE2 CENTER")->name == snare + " Snares Off"
              && is ("TOM1 CENTER", "Tom 1", 43, true) && is ("TOM1 RIMSHOT", "Tom 1", 58, true)
              && is ("HATS BOW OPEN4", "Hi-Hat", 46, true) && is ("HATS EDGE CLOSED", "Hi-Hat", 81, true)
              && is ("HATS BOW TIGHT", "Hi-Hat", 74, false) && is ("HATS PEDAL CLOSED", "Hi-Hat", 44, true)
              && is ("RIDE1 BELL", "Ride 1", 53, true) && is ("CRASH4 EDGE", "Crash 4", 52, true) && is ("CRASH4 BELL", "Crash 4", 96, false)
              && is ("STACK1 EDGE", "Stack 1", 99, true)
              && is ("CRASH1 EDGE", "Crash 1", 52, false)   // DW also plays Crash 4 from 52
              && find ("CRASH1 EDGE")->pluginName.contains ("note shared with Zildjian 19'' Sweet K Crash")
              && is ("PERC COWBELL MAIN", "Cowbell", 102, true) && is ("PERC COWBELL2 MAIN", "Cowbell 2", 103, true)
              && m.zones.size() == 19;

    juce::String notSoundworks;
    emk::KitMap::fromSoundworks ("<Root/>", notSoundworks);
    ok = ok && notSoundworks.isNotEmpty() && emk::KitMap::soundworksTextIn (juce::MemoryBlock ("PlugVersion 3", 13)).isEmpty();

    if (const auto path = juce::SystemStats::getEnvironmentVariable ("SOUNDWORKS_STATE", {}); path.isNotEmpty())
    {
        juce::MemoryBlock real;
        juce::File (path).loadFileAsData (real);
        juce::String realError;
        const auto r = emk::KitMap::fromSoundworks (emk::KitMap::soundworksTextIn (real), realError);
        std::printf ("DW Soundworks state: %s, %d zones, error '%s'\n", r.kitName.toRawUTF8(), r.zones.size(), realError.toRawUTF8());
        for (const auto& z : r.zones)
            std::printf ("  %-10s %-20s %3d %s %-24s %s\n", z.piece.toRawUTF8(), z.label.toRawUTF8(), z.note, z.enabled ? "on " : "off",
                         z.pluginName.toRawUTF8(), z.name.toRawUTF8());
        ok = ok && realError.isEmpty();
    }
    if (! ok)
        std::printf ("soundworksKitReads failed (%s)\n", error.toRawUTF8());
    return ok;
}

/** A MODO DRUM kit, built as MODO DRUM stores it (a binary ValueTree after its own header). */
bool modoKitReads()
{
    auto slot = [] (const char* type, std::initializer_list<std::pair<const char*, int>> artics)
    {
        juce::ValueTree s (type);
        for (const auto& [name, note] : artics)
            s.appendChild (juce::ValueTree (name, { { "MIDINote", note } }), nullptr);
        return s;
    };
    auto bus = [] (const char* type, const char* element, bool on)
    {
        juce::ValueTree b (type, { { "isEnabled", on ? "1" : "0" } });
        b.appendChild (juce::ValueTree ("ELEMENT", { { "referencedElementId", element } }), nullptr);
        return b;
    };
    juce::ValueTree session ("SESSION_STATE"), project ("PROJECT", { { "presetName", "METAL" } }), midiMap ("MIDI_MAP");
    project.appendChild (bus ("Slot2Cymbal", "Crash16_Custom", true), nullptr);
    project.appendChild (bus ("Slot4Cymbal", "Splash10_Vintage", false), nullptr);
    project.appendChild (bus ("Slot6Ride", "Ride24_Custom", true), nullptr);
    project.appendChild (bus ("Slot8RightKick", "Kick_Metal", true), nullptr);
    project.appendChild (bus ("Slot14Snare", "Snare_Metal", true), nullptr);
    project.appendChild (bus ("Slot15HiHat", "HiHat_Custom", true), nullptr);
    project.appendChild (bus ("Slot19FloorTom0", "FloorTom0_Metal", true), nullptr);
    project.appendChild (bus ("Slot22Tom0", "Tom0_Metal", true), nullptr);
    project.appendChild (bus ("SlotCowbell", "Cowbell", true), nullptr);
    midiMap.appendChild (slot ("Slot2Cymbal", { { "Edge", 49 }, { "Catch", 66 } }), nullptr);
    midiMap.appendChild (slot ("Slot4Cymbal", { { "Edge", 55 } }), nullptr);
    midiMap.appendChild (slot ("Slot6Ride", { { "Edge", 51 }, { "Bell", 53 } }), nullptr);
    midiMap.appendChild (slot ("Slot8RightKick", { { "Beater", 36 }, { "BeaterBuried", 35 } }), nullptr);
    midiMap.appendChild (slot ("Slot14Snare", { { "SnareL", 38 }, { "SnareR", 62 }, { "SideStick", 37 }, { "RimShotL", 40 } }), nullptr);
    midiMap.appendChild (slot ("Slot15HiHat", { { "ClosedTip", 42 }, { "ClosedShank", 30 }, { "HalfOpenTip", 46 },
                                                { "OpenShank", 31 }, { "FootClosed", 44 }, { "FootOpen", 32 }, { "SuperTight", 29 } }), nullptr);
    midiMap.appendChild (slot ("Slot19FloorTom0", { { "TomL", 45 }, { "TomR", 69 } }), nullptr);
    midiMap.appendChild (slot ("Slot22Tom0", { { "TomL", 50 } }), nullptr);
    midiMap.appendChild (slot ("SlotCowbell", { { "Normal", 56 } }), nullptr);
    session.appendChild (project, nullptr);
    session.appendChild (midiMap, nullptr);
    juce::MemoryOutputStream state;
    state.write ("VstW\0\0\0\x08", 8);
    session.writeToStream (state);

    juce::String error;
    const auto map = emk::KitMap::fromModo (state.getMemoryBlock(), error);
    auto noteOf = [&map] (const juce::String& piece, const juce::String& label, bool on = true)
    {
        for (const auto& z : map.zones)
            if (z.piece == piece && z.label == label)
                return z.enabled == on ? z.note : -2;
        return -1;
    };
    juce::StringArray pieces;
    for (const auto& z : map.zones)
        pieces.addIfNotAlreadyThere (z.piece);
    bool ok = error.isEmpty() && map.kitName == "METAL" && emk::KitMap::isModoState (state.getMemoryBlock())
              && ! emk::KitMap::isModoState (juce::MemoryBlock ("PlugVersion 3", 13))
              && pieces.joinIntoString (",") == "Kick,Snare,Hi-Hat,Tom 1,Tom 2,Crash 1,Ride 1,Cowbell"
              && noteOf ("Kick", "KICK1") == 36 && noteOf ("Kick", "KICK1 BURIED", false) == 35
              && noteOf ("Snare", "SNARE1 CENTER") == 38 && noteOf ("Snare", "SNARE1 RIMSHOT") == 40
              && noteOf ("Snare", "SNARE1 SIDESTICK") == 37 && noteOf ("Snare", "SNARE1 CENTER R", false) == 62
              && noteOf ("Hi-Hat", "HATS BOW CLOSED") == 42 && noteOf ("Hi-Hat", "HATS EDGE CLOSED") == 30
              && noteOf ("Hi-Hat", "HATS BOW OPEN2") == 46 && noteOf ("Hi-Hat", "HATS EDGE OPEN4") == 31
              && noteOf ("Hi-Hat", "HATS PEDAL CLOSED") == 44 && noteOf ("Hi-Hat", "HATS FOOT SPLASH") == 32
              && noteOf ("Hi-Hat", "HATS SUPERTIGHT", false) == 29
              && noteOf ("Tom 1", "TOM1 CENTER") == 50 && noteOf ("Tom 2", "TOM2 CENTER") == 45
              && noteOf ("Crash 1", "CRASH1 EDGE") == 49 && noteOf ("Crash 1", "CRASH1 CHOKE", false) == 66
              && noteOf ("Ride 1", "RIDE1 BOW") == 51 && noteOf ("Ride 1", "RIDE1 BELL") == 53
              && noteOf ("Cowbell", "PERC COWBELL MAIN") == 56;
    for (const auto& z : map.zones)
        if (z.piece == "Crash 1")
            ok = ok && z.name == "Crash 16 Custom" && z.library == "MODO DRUM" && z.instrument == "MODO DRUM/Crash16_Custom";
    if (const auto real = juce::SystemStats::getEnvironmentVariable ("MODO_STATE", {}); real.isNotEmpty())
    {
        juce::MemoryBlock data;
        juce::File (real).loadFileAsData (data);
        juce::String realError;
        const auto realMap = emk::KitMap::fromModo (data, realError);
        std::cout << "real state: " << realMap.kitName << ", " << realMap.zones.size() << " zones " << realError << "\n";
        for (const auto& z : realMap.zones)
            std::cout << "  " << z.piece << " | " << z.name << " | " << z.label << " | " << z.note << " | " << (z.enabled ? "on" : "off") << " | " << z.pluginName << "\n";
    }
    juce::String notModo;
    emk::KitMap::fromModo (juce::MemoryBlock ("hello", 5), notModo);
    std::printf ("modo kit %s (%d zones)\n", ok && notModo.isNotEmpty() ? "ok" : "WRONG", map.zones.size());
    return ok && notModo.isNotEmpty();
}

/** Drum-like test sounds: a low thud, a short noisy crack, a long bright ring. */
juce::AudioBuffer<float> testHit (int kind, double rate, float gain, int seed)
{
    juce::Random random (seed);
    juce::AudioBuffer<float> b (1, (int) (0.5 * rate));
    double lowpass = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / rate;
        double x = 0.0;
        if (kind == 0) x = std::sin (juce::MathConstants<double>::twoPi * 60.0 * t) * std::exp (-t / 0.15);
        if (kind == 1) { lowpass += 0.3 * ((random.nextFloat() * 2.0 - 1.0) - lowpass); x = lowpass * std::exp (-t / 0.05); }
        if (kind == 2) x = (random.nextFloat() * 2.0 - 1.0) * std::exp (-t / 1.5) * (i % 2 == 0 ? 1.0 : -1.0);
        b.setSample (0, i, gain * (float) x);
    }
    return b;
}

/** Each test sound, heard at another rate and strength, is matched to its own note. */
bool soundPrintsMatch()
{
    std::map<int, emk::SoundPrint> prints;
    for (int kind = 0; kind < 3; ++kind)
    {
        const auto ref = testHit (kind, 44100.0, 0.9f, 1);
        prints[36 + kind] = emk::SoundPrint::of (ref.getReadPointer (0), ref.getNumSamples(), 44100.0);
    }
    bool ok = true;
    for (int kind = 0; kind < 3; ++kind)
    {
        const auto heard = testHit (kind, 48000.0, 0.3f, 7);
        ok = ok && emk::closestNote (prints, emk::SoundPrint::of (heard.getReadPointer (0), heard.getNumSamples(), 48000.0)) == 36 + kind;
    }
    std::printf ("sound prints %s\n", ok ? "ok" : "WRONG");
    return ok;
}

/** Plays a note from "MIDI input" and a click in the plugin's window through LiveHost. */
bool liveHostWorks()
{
    FakeDrums drums;
    FakeDevice device;
    LiveHost live;
    juce::Array<int> learned;
    live.onPluginNote = [&] (int note) { learned.add (note); };
    live.audioDeviceAboutToStart (&device);
    live.setProcessor (&drums);

    juce::AudioBuffer<float> out (2, 256);
    auto block = [&]
    {
        out.clear();
        live.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, {});
        return out.getMagnitude (0, 0, 256) + out.getMagnitude (1, 0, 256);
    };

    const float before = block();
    live.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100)
                                                 .withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
    juce::Thread::sleep (20);
    float fromInput = 0.0f;
    for (int i = 0; i < 4; ++i)
        fromInput = juce::jmax (fromInput, block());
    live.poll();
    const bool inputNotLearned = learned.isEmpty();   // pads are learned from the MIDI input itself

    drums.clickNote = 42;
    block();
    live.poll();
    // A click that only makes a sound is caught for learning by ear.
    int heard = 0;
    live.onHeardHit = [&] (const juce::AudioBuffer<float>& hit, double rate)
    {
        if (hit.getNumSamples() == (int) (LiveHost::captureSeconds * rate) && hit.getMagnitude (0, 0, 256) > 0.1f)
            ++heard;
    };
    live.setListening (true);
    for (int i = 0; i < 400; ++i)   // let the earlier hits die away first
        block();
    live.poll();
    const int heardBefore = heard;
    drums.clickSilently = true;
    for (int i = 0; i < 80; ++i)
        block();
    live.poll();

    live.setProcessor (nullptr);
    const float afterStop = block();

    const bool ok = before <= 0.0f && fromInput > 0.0f && inputNotLearned && learned == juce::Array<int> { 42 }
                    && afterStop <= 0.0f && heardBefore == 0 && heard == 1;
    std::printf ("live host %s (input level %.2f, plugin notes %d, clicks heard %d)\n", ok ? "ok" : "WRONG", fromInput,
                 learned.size(), heard);
    return ok;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s OUTPUT_FOLDER\n", argv[0]);
        return 2;
    }
    const juce::File root (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));

    FakeDrums plugin;
    auto map = emk::KitMap::rolandEDrum();
    map.kitName = "Test Kit";
    emk::BatchRecorder::Settings settings;
    settings.hitsPerZone = 8;
    settings.render.maxSeconds = 3.0;

    int enabled = 0;
    for (const auto& z : map.zones)
        enabled += z.enabled ? 1 : 0;

    int snare = -1;
    for (int i = 0; i < map.zones.size(); ++i)
        if (map.zones[i].label == "SNARE1 CENTER")
            snare = i;
    const auto picture = emk::WavToMci::thumbnail (94, 63);
    map.setPieceName (snare, "14\" Brass Snare");
    map.setPieceLibrary (snare, "My Library");
    map.setPieceImage (snare, picture);

    int calls = 0;
    const auto report = emk::BatchRecorder::record (plugin, map, settings, root, [&] (const auto&) { ++calls; return true; });
    std::printf ("%s\nprogress calls %d, CC4 messages %d\n", report.toRawUTF8(), calls, plugin.ccCount);

    // The name, library and image given to a piece end up in its Mimic instrument.
    const auto snareFolder = emk::BatchRecorder::fileFor (root, map.kitName, map.zones[snare], 1, 1)
                                 .getParentDirectory().getParentDirectory();
    const auto mimicDir = root.getChildFile ("mimic info test");
    const auto built = emk::WavToMci::run (snareFolder, mimicDir, {}, [] (const juce::String&) {}, [] (double) { return true; });
    bool infoOk = built.written.size() == 1 && built.written[0].endsWith ("14 Brass Snare.mci");
    if (infoOk)
    {
        const auto mci = emk::mci::readMci (juce::File (built.written[0]));
        infoOk = mci.getString ("INST0insnam") == "14\" Brass Snare" && mci.getString ("INST0libnam") == "My Library"
                 && mci.image == picture;
    }
    // Percussion: zones named in the label take those places, the rest fill in by label order.
    {
        emk::KitMap perc;
        perc.kitName = "Perc Kit";
        perc.zones.add ({ "Octobans", "PERC 2 HEAD", 31 });
        perc.zones.add ({ "Octobans", "PERC 1 HEAD", 30 });
        perc.zones.add ({ "Octobans", "PERC OCTOBAN4 ADD2", 33 });
        emk::BatchRecorder::record (plugin, perc, settings, root, [] (const auto&) { return true; });
        const auto made = emk::WavToMci::run (root.getChildFile ("Perc Kit"), root.getChildFile ("perc mimic"), {},
                                              [] (const juce::String&) {}, [] (double) { return true; });
        bool percOk = made.written.size() == 1;
        if (percOk)
        {
            const auto mci = emk::mci::readMci (juce::File (made.written[0]));
            percOk = mci.getString ("INST0instyp") == "Perc" && mci.getInt ("INST0numart") == 3
                     && mci.getString ("INST0artic0artn") == "Main" && mci.getString ("INST0artic2artn") == "Additional 2";
        }
        std::printf ("percussion %s (%s)\n", percOk ? "ok" : "WRONG", made.toString().toRawUTF8());
        infoOk = infoOk && percOk;
    }

    // Any picture becomes the Mimic's 94x63 PNG, whole and centred on black.
    juce::Image wide (juce::Image::ARGB, 400, 100, true);
    wide.clear (wide.getBounds(), juce::Colours::red);
    const auto small = emk::MimicImage::load (emk::MimicImage::png (wide));
    const bool imageOk = small.getWidth() == 94 && small.getHeight() == 63 && small.getPixelAt (47, 31).getRed() > 200
                         && small.getPixelAt (47, 2).getRed() < 30;
    infoOk = infoOk && imageOk;

    // A name read off a plugin-style picture: light letters on a dark panel.
    juce::Image plate (juce::Image::RGB, 260, 22, true);
    {
        juce::Graphics g (plate);
        g.fillAll (juce::Colour (0xff2b2d33));
        g.setColour (juce::Colour (0xffd8d8d8));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("DW Timeless Timber Maple", plate.getBounds(), juce::Justification::centredLeft);
    }
    juce::String ocrError;
    const auto read = ScreenTools::readText (plate, ocrError);
    std::printf ("text read: \"%s\" %s\n", read.toRawUTF8(), ocrError.toRawUTF8());
    if (ocrError.isEmpty())
        infoOk = infoOk && read.contains ("Timeless Timber Maple");
    if (const auto shot = juce::SystemStats::getEnvironmentVariable ("OCR_IMAGE", {}); shot.isNotEmpty())
    {
        // OCR_BOXES="x,y,w,h;x,y,w,h": read each box of a screenshot, to try real plugin windows.
        const auto screenshot = juce::ImageFileFormat::loadFrom (juce::File (shot));
        for (const auto& b : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("OCR_BOXES", {}), ";", ""))
        {
            const auto v = juce::StringArray::fromTokens (b, ",", "");
            const juce::Rectangle<int> r (v[0].getIntValue(), v[1].getIntValue(), v[2].getIntValue(), v[3].getIntValue());
            juce::String e;
            std::printf ("box %s: \"%s\" %s\n", b.toRawUTF8(), ScreenTools::readText (screenshot.getClippedImage (r).createCopy(), e).toRawUTF8(),
                         e.toRawUTF8());
        }
    }
    using K = emk::KitMap;
    const bool fileNamesOk = K::nameFromFileName ("YamahaVintage.png") == "Yamaha Vintage"
                             && K::nameFromFileName ("5x14_DW_Timeless_Timber_Maple.png") == "5x14 DW Timeless Timber Maple"
                             && K::nameFromFileName ("DWTimelessTimberMaple.jpg") == "DW Timeless Timber Maple"
                             && K::nameFromFileName ("22in K Custom Ride.png") == "22in K Custom Ride"
                             && K::nameFromFileName ("Screenshot_20261008.png").isEmpty();
    std::printf ("names from files %s (%s)\n", fileNamesOk ? "ok" : "WRONG", K::nameFromFileName ("DWTimelessTimberMaple.jpg").toRawUTF8());
    infoOk = infoOk && fileNamesOk;
    std::printf ("piece info %s, picture %s\n", infoOk ? "ok" : "WRONG", imageOk ? "ok" : "WRONG");

    // Only notes 20 to 70 make a sound.
    const auto sounding = emk::BatchRecorder::findSoundingNotes (plugin, settings.render, [] (int) { return true; });
    std::printf ("sounding notes %d (%d to %d)\n", sounding.size(), sounding.getFirst(), sounding.getLast());

    // Maps survive a trip through a .csv file.
    juce::String csvError;
    const auto reread = emk::KitMap::fromCsv (map.toCsv(), csvError);
    const bool csvOk = csvError.isEmpty() && reread.toCsv() == map.toCsv();
    juce::String badError;
    emk::KitMap::fromCsv ("Kick,KICK1,abc", badError);

    // Hits per zone that fit the Mimic's wave-cell limits.
    auto hitsOf = [&map] (const juce::String& label)
    {
        for (int i = 0; i < map.zones.size(); ++i)
            if (map.zones[i].label == label)
                return map.hitsFor (i, 127);
        return -1;
    };
    const bool limitsOk = hitsOf ("KICK1") == 64 && hitsOf ("SNARE1 CENTER") == 100 && hitsOf ("TOM1 RIMSHOT") == 100
                          && hitsOf ("CRASH1 EDGE") == 99 && hitsOf ("RIDE1 BELL") == 99 && hitsOf ("HATS BOW OPEN2") == 32;
    std::printf ("limits %s\n", limitsOk ? "ok" : "WRONG");

    // A count set by hand: the snare's other ticked zones share what's left of its 300.
    auto chosen = map;
    int sidestick = -1;
    for (int i = 0; i < chosen.zones.size(); ++i)
        if (chosen.zones[i].label == "SNARE1 SIDESTICK")
            sidestick = i;
    const int set = chosen.setHits (sidestick, 30);
    const int tooMany = emk::KitMap (chosen).setHits (sidestick, 1000);
    auto hitsIn = [&chosen] (const juce::String& label, int most)
    {
        for (int i = 0; i < chosen.zones.size(); ++i)
            if (chosen.zones[i].label == label)
                return chosen.hitsFor (i, most);
        return -1;
    };
    juce::String err;
    const bool setOk = set == 30 && hitsIn ("SNARE1 SIDESTICK", 127) == 30 && hitsIn ("SNARE1 CENTER", 200) == 135
                       && hitsIn ("SNARE1 CENTER", 127) == 127 && tooMany == 298
                       && emk::KitMap::fromCsv (chosen.toCsv(), err).toCsv() == chosen.toCsv()
                       && emk::KitMap::fromValueTree (chosen.toValueTree()).zones[sidestick].hits == 30;
    std::printf ("chosen hits %s (sidestick %d, center %d, at most %d)\n", setOk ? "ok" : "WRONG", set,
                 hitsIn ("SNARE1 CENTER", 200), tooMany);

    // Note names from plugins.
    const auto g1 = emk::KitMap::guessFromName ("Ride 1 Bell"), g2 = emk::KitMap::guessFromName ("Snare Rimshot"),
               g3 = emk::KitMap::guessFromName ("Hats Open 2 Edge"), g4 = emk::KitMap::guessFromName ("Tom 3 Rim");
    const bool namesOk = g1.second == "RIDE1 BELL" && g2.second == "SNARE1 RIMSHOT" && g3.second == "HATS EDGE OPEN2"
                         && g4.second == "TOM3 RIMSHOT" && emk::KitMap::guessFromName ("Tambourine").first.isEmpty();
    std::printf ("names %s (%s, %s, %s, %s)\n", namesOk ? "ok" : "WRONG", g1.second.toRawUTF8(), g2.second.toRawUTF8(),
                 g3.second.toRawUTF8(), g4.second.toRawUTF8());

    // Velocity spread and names are what wav2mci depends on.
    const auto v = emk::BatchRecorder::velocities (5, 1, 127);
    const bool ok = v == juce::Array<int> { 127, 96, 64, 33, 1 }
                    && emk::BatchRecorder::safeName ("A - B/C") == "A BC"
                    && calls == enabled * settings.hitsPerZone
                    && plugin.ccCount > 0 && plugin.ccCount <= 11
                    && sounding.size() == 51 && sounding.getFirst() == 20 && sounding.getLast() == 70
                    && csvOk && badError.isNotEmpty() && limitsOk && namesOk && setOk && liveHostWorks() && soundPrintsMatch() && sd3KitReads() && bfd3KitReads() && modoKitReads() && ugritoneKitReads() && soundworksKitReads() && infoOk;
    std::printf ("velocities %s, name \"%s\"\n%s\n", juce::String (v[1]).toRawUTF8(),
                 emk::BatchRecorder::safeName ("A - B/C").toRawUTF8(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
