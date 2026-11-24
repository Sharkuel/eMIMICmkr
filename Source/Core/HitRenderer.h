#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace emk
{
/** Plays single hits through a hosted plugin offline and captures the audio. */
class HitRenderer
{
public:
    struct Settings
    {
        double sampleRate = 44100.0;  // the Mimic's rate
        int blockSize = 512;
        double maxSeconds = 30.0;       // longest tail kept (rides, gongs); the converter cuts and fades it
        double silenceDb = -80.0;       // a hit starts at the first sound above this
        double tailDb = -90.0;          // and ends once it stays this far below its peak...
        double floorDb = -100.0;        // ...or below this, whichever is louder (plugin noise)...
        double silenceSeconds = 0.25;   // ...for this long
        double noteLengthSeconds = 0.1; // time between note-on and note-off
        double giveUpSeconds = 1.0;     // a note still silent after this plays nothing

        /** Separate mics: the plugin's output buses (0 = its main output) carrying the close
            mics, the overheads and the room; -1 = none. With any set, a hit comes back as five
            channels, close (the bus averaged to mono), OH left and right, room left and right,
            and the main output mix is not used. A mono bus fills both sides of a pair. */
        struct MicBuses
        {
            int close = -1, overheads = -1, room = -1;
            bool enabled() const { return close >= 0 || overheads >= 0 || room >= 0; }
        } mics;
    };

    HitRenderer (juce::AudioPluginInstance& plugin, Settings settings);
    ~HitRenderer();

    /** Renders one hit and returns it as mono (or the five mic channels, see MicBuses), trimmed
        at the first sound. cc4 >= 0 sets the hi-hat pedal (MIDI CC4) and lets the plugin settle first. */
    juce::AudioBuffer<float> renderHit (int note, int velocity, int cc4 = -1);

    /** The mono mix of a five-channel mic hit (close + OH average + room average), as the
        plugin's own main output sums them; a mono hit as it is. */
    static juce::AudioBuffer<float> mixDown (const juce::AudioBuffer<float>&);

    /** The names of the plugin's output buses ("Main", "Out 3/4"...), for picking MicBuses. */
    static juce::StringArray outputBusNames (juce::AudioPluginInstance&);

private:
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void waitForSilence();
    void addMainOutput (juce::AudioBuffer<float>& dest, int destStart, const juce::AudioBuffer<float>& src, int n);
    void addMics (juce::AudioBuffer<float>& dest, int destStart, const juce::AudioBuffer<float>& src, int n);
    int busChannels() const;

    juce::AudioPluginInstance& plugin;
    Settings settings;
    int lastCc4 = -1;   // the pedal is only moved when a hit needs a different position
};
} // namespace emk
