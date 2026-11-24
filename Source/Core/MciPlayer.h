#pragma once
#include "Mci.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace emk
{
/** Plays a Mimic instrument the way the Mimic does, to hear it before it goes on the Mimic.

    Follows Mimic OS 2's player (tools/xplay in Pearl-Mimic-OS-2.0): a hit plays the highest
    velocity layer whose minvel is at or below it, the layer's round robins in turn, at a gain
    from the layer's minvol (at its lowest velocity) to maxvol (just under the next layer),
    times the sample's svol and the articulation's vol. Every mic plays at its micv, a mono mic
    in the middle, a stereo one left and right. */
class MciPlayer
{
public:
    explicit MciPlayer (mci::MciFile);

    struct Layer
    {
        int minVelocity = 0;
        std::vector<int> samples;   // pool indices of the round robins
        float minVolume = 0.0f, maxVolume = 1.0f;
    };
    struct Articulation
    {
        juce::String name;
        float volume = 1.0f;
        std::vector<Layer> layers;
    };
    const std::vector<Articulation>& articulations() const { return arts; }
    const mci::MciFile& file() const { return f; }

    /** The layer a hit at this velocity plays. */
    static int layerFor (const Articulation&, int velocity);

    /** One hit, stereo at 44.1 kHz; the next hit of the same layer takes the next round robin.
        Empty for an articulation with no layers (an empty Choke Ring). */
    juce::AudioBuffer<float> hit (int articulation, int velocity);
    /** A round robin of a layer at full layer volume, as the Mimic would mix it. */
    juce::AudioBuffer<float> sample (int poolIndex, float gain) const;

    /** Every layer's loudness: the mean of its round robins' peaks, in dBFS, as the Mimic plays
        a hit at the layer's lowest velocity. Soft to hard. */
    std::vector<double> layerLevels (int articulation) const;

    /** Layers that break the rise from soft to hard: louder than the layer above, or more than
        `jumpDb` above the layer below. "Layer 4 (from velocity 61) is 2.1 dB louder than layer 5". */
    juce::StringArray layerProblems (int articulation, double jumpDb = 6.0) const;

private:
    mci::MciFile f;
    std::vector<Articulation> arts;
    std::vector<bool> stereo;   // per mic
    std::vector<float> micLevel, svol;
    std::map<std::pair<int, int>, int> nextRoundRobin;
};
} // namespace emk
