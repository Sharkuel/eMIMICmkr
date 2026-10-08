// Records a kit from a stand-in drum plugin, so the batch recorder can be checked
// without a commercial plugin. Usage: eMIMICmkr_tests OUTPUT_FOLDER
// tests/test_batch_recording.py then converts the result with wav2mci.
#include "../../Source/Core/BatchRecorder.h"
#include "../../Source/App/LiveHost.h"
#include "../../Source/Core/SoundPrint.h"

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
    bool rimclickOn = true;
    const bool ok = error.isEmpty()
                    && noteOf ("Kick", "KICK1") == 36 && noteOf ("Snare", "SNARE1 CENTER") == 38
                    && noteOf ("Snare", "SNARE1 SIDESTICK") == 37 && noteOf ("Snare", "SNARE1 RIMSHOT") == 76
                    && noteOf ("Snare 2", "SNARE2 CENTER") == 67 && noteOf ("Tom 1", "TOM1 CENTER") == 48
                    && noteOf ("Tom 1", "TOM1 RIMSHOT") == 81 && noteOf ("Tom 1", "TOM1 RIMCLICK", &rimclickOn) == 82
                    && ! rimclickOn && noteOf ("Tom 5", "TOM5 CENTER") == 41
                    && noteOf ("Ride 1", "RIDE1 BOW") == 51 && noteOf ("Ride 1", "RIDE1 BELL") == 53
                    && noteOf ("Ride 1", "RIDE1 EDGE") == 59 && noteOf ("Crash 2", "CRASH2 EDGE") == 57
                    && noteOf ("Splash 1", "SPLASH1 EDGE") == 55 && noteOf ("Hi-Hat", "HATS EDGE OPEN2") == 7
                    && noteOf ("Hi-Hat", "HATS BOW CLOSED") == 8 && noteOf ("Hi-Hat", "HATS PEDAL CLOSED") == 21
                    && noteOf ("Hi-Hat", "HATS FOOT SPLASH") == 23;
    juce::String notSd3;
    emk::KitMap::fromSd3 ("piece,zone,note", notSd3);
    std::printf ("sd3 kit %s (%d zones, snare rimshot %d)\n", ok && notSd3.isNotEmpty() ? "ok" : "WRONG",
                 map.zones.size(), noteOf ("Snare", "SNARE1 RIMSHOT"));
    return ok && notSd3.isNotEmpty();
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
                    && csvOk && badError.isNotEmpty() && limitsOk && namesOk && setOk && liveHostWorks() && soundPrintsMatch() && sd3KitReads();
    std::printf ("velocities %s, name \"%s\"\n%s\n", juce::String (v[1]).toRawUTF8(),
                 emk::BatchRecorder::safeName ("A - B/C").toRawUTF8(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
