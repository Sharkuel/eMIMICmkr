// Records a kit from a stand-in drum plugin, so the batch recorder can be checked
// without a commercial plugin. Usage: eMIMICmkr_tests OUTPUT_FOLDER
// tests/test_batch_recording.py then converts the result with wav2mci.
#include "../../Source/Core/BatchRecorder.h"
#include "../../Source/App/LiveHost.h"

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
        if (clickNote >= 0)   // a click on a drum in the plugin's window: heard, and sent out as MIDI
        {
            midi.addEvent (juce::MidiMessage::noteOn (10, clickNote, (juce::uint8) 100), 0);
            clickNote = -1;
        }
    }

    int ccCount = 0, clickNote = -1;

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
    live.deliverNotes();
    const bool inputNotLearned = learned.isEmpty();   // pads are learned from the MIDI input itself

    drums.clickNote = 42;
    block();
    live.deliverNotes();
    live.setProcessor (nullptr);
    const float afterStop = block();

    const bool ok = before <= 0.0f && fromInput > 0.0f && inputNotLearned && learned == juce::Array<int> { 42 }
                    && afterStop <= 0.0f;
    std::printf ("live host %s (input level %.2f, plugin notes %d)\n", ok ? "ok" : "WRONG", fromInput, learned.size());
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

    int calls = 0;
    const auto report = emk::BatchRecorder::record (plugin, map, settings, root, [&] (const auto&) { ++calls; return true; });
    std::printf ("%s\nprogress calls %d, CC4 messages %d\n", report.toRawUTF8(), calls, plugin.ccCount);

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
                    && plugin.ccCount > 0
                    && sounding.size() == 51 && sounding.getFirst() == 20 && sounding.getLast() == 70
                    && csvOk && badError.isNotEmpty() && limitsOk && namesOk && liveHostWorks();
    std::printf ("velocities %s, name \"%s\"\n%s\n", juce::String (v[1]).toRawUTF8(),
                 emk::BatchRecorder::safeName ("A - B/C").toRawUTF8(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
