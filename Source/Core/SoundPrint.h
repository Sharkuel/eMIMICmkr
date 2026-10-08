#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <vector>

namespace emk
{
/** A rough fingerprint of a drum hit: its spectrum in bands, minus its overall level, plus how
    fast it decays. Two hits of the same zone at different strengths come out close; a kick and
    a snare, or a ride bell and a ride bow, far apart. Lets "Learn notes" tell which drum was
    clicked in a plugin's window when the plugin doesn't send that click out as MIDI. */
struct SoundPrint
{
    static constexpr double seconds = 0.3;   // audio needed from the start of the hit

    /** mono starts at the hit; rate is its sample rate (any rate: bands are in Hz). */
    static SoundPrint of (const float* mono, int numSamples, double rate);

    float distanceTo (const SoundPrint&) const;
    bool isEmpty() const { return features.empty(); }

    std::vector<float> features;
};

/** The note whose print is closest to heard, or -1. ratio gets the runner-up's distance over
    the best one's (near 1 = a toss-up), to tell a sure match from a guess. */
int closestNote (const std::map<int, SoundPrint>& prints, const SoundPrint& heard, float* ratio = nullptr);
} // namespace emk
