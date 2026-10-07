// Records a kit from a stand-in drum plugin, so the batch recorder can be checked
// without a commercial plugin. Usage: eMIMICmkr_tests OUTPUT_FOLDER
// tests/test_batch_recording.py then converts the result with wav2mci.
#include "../../Source/Core/BatchRecorder.h"

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
    }

    int ccCount = 0;

private:
    double level = 0.0, decay = 0.0;
    juce::Random random { 1 };
};
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

    // Velocity spread and names are what wav2mci depends on.
    const auto v = emk::BatchRecorder::velocities (5, 1, 127);
    const bool ok = v == juce::Array<int> { 127, 96, 64, 33, 1 }
                    && emk::BatchRecorder::safeName ("A - B/C") == "A BC"
                    && calls == enabled * settings.hitsPerZone
                    && plugin.ccCount > 0
                    && sounding.size() == 51 && sounding.getFirst() == 20 && sounding.getLast() == 70
                    && csvOk && badError.isNotEmpty();
    std::printf ("velocities %s, name \"%s\"\n%s\n", juce::String (v[1]).toRawUTF8(),
                 emk::BatchRecorder::safeName ("A - B/C").toRawUTF8(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
