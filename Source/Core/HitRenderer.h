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
        double maxSeconds = 20.0;       // longest tail kept (rides, gongs)
        double silenceDb = -80.0;       // a hit ends once it stays below this...
        double silenceSeconds = 0.25;   // ...for this long
        double noteLengthSeconds = 0.1; // time between note-on and note-off
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
};
} // namespace emk
