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
        double sampleRate = 48000.0;
        int blockSize = 512;
        double maxSeconds = 12.0;       // longest tail kept (cymbals)
        double silenceDb = -80.0;       // a hit ends once it stays below this...
        double silenceSeconds = 0.25;   // ...for this long
        double noteLengthSeconds = 0.1; // time between note-on and note-off
    };

    HitRenderer (juce::AudioPluginInstance& plugin, Settings settings);
    ~HitRenderer();

    /** Renders one hit and returns it as mono, trimmed at the first sound. */
    juce::AudioBuffer<float> renderHit (int note, int velocity);

private:
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void waitForSilence();

    juce::AudioPluginInstance& plugin;
    Settings settings;
};
} // namespace emk
