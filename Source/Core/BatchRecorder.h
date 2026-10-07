#pragma once
#include "HitRenderer.h"
#include "KitMap.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace emk
{
/** Records every enabled zone of a kit at many velocities, in the folder layout
    tools/mci/wav2mci.py reads:

        <root>/<kit>/<kit> <piece>/<LABEL>/<kit> <piece> - <LABEL> - 001v127.wav

    one folder per zone, mono 24-bit 44.1 kHz, hardest hit first. */
class BatchRecorder
{
public:
    struct Settings
    {
        int hitsPerZone = 127;          // the most per zone...
        bool fitMimicLimits = true;     // ...but no more than the Mimic can hold (KitMap::hitsFor)
        int softestVelocity = 1;
        int hardestVelocity = 127;
        HitRenderer::Settings render;
    };

    struct Progress
    {
        int done = 0, total = 0;
        juce::String current;   // "Ride - RIDE1 BELL v96"
    };

    /** count velocities from hardest to softest, evenly spread; repeats when count
        exceeds the range, which the plugin plays as round robins. */
    static juce::Array<int> velocities (int count, int softest, int hardest);

    /** Strips characters file systems reject, and " - ", which separates the parts of a hit's name. */
    static juce::String safeName (const juce::String&);

    static juce::File fileFor (const juce::File& root, const juce::String& kitName, const KitZone&,
                               int hitIndex, int velocity);

    /** Returns a short report. onProgress returns false to stop early. */
    static juce::String record (juce::AudioPluginInstance&, const KitMap&, const Settings&,
                                const juce::File& root, std::function<bool (const Progress&)> onProgress);

    /** Plays every MIDI note once at velocity 100 and returns those that make a sound.
        onProgress gets the note being tried and returns false to stop. */
    static juce::Array<int> findSoundingNotes (juce::AudioPluginInstance&, HitRenderer::Settings,
                                               std::function<bool (int note)> onProgress);

    static bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
};
} // namespace emk
