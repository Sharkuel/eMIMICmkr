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
    };

    HitRenderer (juce::AudioPluginInstance& plugin, Settings settings);
    ~HitRenderer();

    /** Renders one hit and returns it as mono, trimmed at the first sound.
        cc4 >= 0 sets the hi-hat pedal (MIDI CC4) and lets the plugin settle first. */
    juce::AudioBuffer<float> renderHit (int note, int velocity, int cc4 = -1);

private:
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void waitForSilence();
    void addMainOutput (juce::AudioBuffer<float>& dest, int destStart, const juce::AudioBuffer<float>& src, int n);

    juce::AudioPluginInstance& plugin;
    Settings settings;
    int lastCc4 = -1;   // the pedal is only moved when a hit needs a different position
};
} // namespace emk
