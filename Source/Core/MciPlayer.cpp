#include "MciPlayer.h"
#include <cmath>

namespace emk
{
MciPlayer::MciPlayer (mci::MciFile file) : f (std::move (file))
{
    const int mics = std::max (1, f.getInt ("INST0micnt", 1));
    for (int m = 0; m < mics; ++m)
    {
        const auto M = "INST0micinf" + std::to_string (m);
        stereo.push_back (f.getInt (M + "isst") != 0);
        micLevel.push_back (f.getFloat (M + "micv", 1.0f));
    }
    svol = f.getFloats ("INST0pool0svol");
    const int n = f.getInt ("INST0numart");
    for (int a = 0; a < n; ++a)
    {
        const auto P = "INST0artic" + std::to_string (a);
        Articulation art;
        art.name = f.getString (P + "artn");
        art.volume = f.getFloat (P + "vol", 1.0f);
        const int layers = f.getInt (P + "numlay");
        for (int l = 0; l < layers; ++l)
        {
            const auto L = P + "veloLay" + std::to_string (l);
            Layer layer;
            layer.minVelocity = f.getInt (L + "minvel");
            layer.minVolume = f.getFloat (L + "minvol", 0.0f);
            layer.maxVolume = f.getFloat (L + "maxvol", 1.0f);
            const auto idx = f.getInts (L + "smpidx");
            const int count = f.getInt (L + "nums");
            for (int r = 0; r < count && r < (int) idx.size(); ++r)
                if (idx[(size_t) r] >= 0)
                    layer.samples.push_back (idx[(size_t) r]);
            if (! layer.samples.empty())
                art.layers.push_back (std::move (layer));
        }
        arts.push_back (std::move (art));
    }
}

int MciPlayer::layerFor (const Articulation& a, int velocity)
{
    int l = 0;
    while (l + 1 < (int) a.layers.size() && a.layers[(size_t) l + 1].minVelocity <= velocity)
        ++l;
    return l;
}

juce::AudioBuffer<float> MciPlayer::sample (int index, float gain) const
{
    const auto values = mci::sample (f, index);
    size_t width = 0;
    for (bool st : stereo)
        width += st ? 2 : 1;
    const auto nchn = (size_t) std::max (1, f.getInts ("INST0pool0nchn").at ((size_t) index));
    // An instrument whose channels don't match its mics (none should): play the first channel.
    const bool byMic = nchn == width;
    const int frames = (int) (values.size() / nchn);
    juce::AudioBuffer<float> out (2, frames);
    out.clear();
    const float scale = gain / 8388608.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto* v = values.data() + (size_t) i * nchn;
        float l = 0.0f, r = 0.0f;
        if (! byMic)
            l = r = 0.5f * (float) v[0];
        else
        {
            size_t c = 0;
            for (size_t m = 0; m < stereo.size(); ++m)
            {
                const float xl = (float) v[c], xr = stereo[m] ? (float) v[c + 1] : xl;
                c += stereo[m] ? 2u : 1u;
                l += 0.5f * xl * micLevel[m];   // pan in the middle: (1 - 0.5) left, 0.5 right, as Mimic OS 2
                r += 0.5f * xr * micLevel[m];
            }
        }
        out.setSample (0, i, l * scale);
        out.setSample (1, i, r * scale);
    }
    return out;
}

juce::AudioBuffer<float> MciPlayer::hit (int articulation, int velocity)
{
    if (! juce::isPositiveAndBelow (articulation, (int) arts.size()) || arts[(size_t) articulation].layers.empty())
        return {};
    const auto& a = arts[(size_t) articulation];
    const int l = layerFor (a, velocity);
    const auto& layer = a.layers[(size_t) l];
    const int top = l + 1 < (int) a.layers.size() ? a.layers[(size_t) l + 1].minVelocity - 1 : 127;
    const float t = top > layer.minVelocity ? juce::jlimit (0.0f, 1.0f, (float) (velocity - layer.minVelocity) / (float) (top - layer.minVelocity))
                                            : 1.0f;
    auto& rr = nextRoundRobin[{ articulation, l }];
    const int index = layer.samples[(size_t) (rr++ % (int) layer.samples.size())];
    const float gain = (layer.minVolume + (layer.maxVolume - layer.minVolume) * t)
                       * (juce::isPositiveAndBelow (index, (int) svol.size()) ? svol[(size_t) index] : 1.0f) * a.volume;
    return sample (index, gain);
}

std::vector<double> MciPlayer::layerLevels (int articulation) const
{
    std::vector<double> out;
    if (! juce::isPositiveAndBelow (articulation, (int) arts.size()))
        return out;
    const auto& a = arts[(size_t) articulation];
    for (const auto& layer : a.layers)
    {
        double sum = 0.0;
        for (auto index : layer.samples)
        {
            const float gain = layer.minVolume * (juce::isPositiveAndBelow (index, (int) svol.size()) ? svol[(size_t) index] : 1.0f) * a.volume;
            // minvol 0 (the softest layer) would read as silence: take its top instead.
            const auto s = sample (index, gain > 0.0f ? gain : layer.maxVolume * (juce::isPositiveAndBelow (index, (int) svol.size()) ? svol[(size_t) index] : 1.0f) * a.volume);
            sum += std::max (s.getMagnitude (0, 0, s.getNumSamples()), s.getMagnitude (1, 0, s.getNumSamples()));
        }
        const double mean = layer.samples.empty() ? 0.0 : sum / (double) layer.samples.size();
        out.push_back (mean > 0.0 ? 20.0 * std::log10 (mean) : -120.0);
    }
    return out;
}

juce::StringArray MciPlayer::layerProblems (int articulation, double jumpDb) const
{
    juce::StringArray out;
    const auto levels = layerLevels (articulation);
    const auto& a = arts[(size_t) articulation];
    auto name = [&] (size_t l) { return "layer " + juce::String ((int) l + 1) + " (from velocity " + juce::String (a.layers[l].minVelocity) + ")"; };
    for (size_t l = 0; l + 1 < levels.size(); ++l)
    {
        const double step = levels[l + 1] - levels[l];
        if (step < -0.5)
            out.add (a.name + ": " + name (l) + " is " + juce::String (-step, 1) + " dB louder than " + name (l + 1));
        else if (step > jumpDb)
            out.add (a.name + ": " + name (l + 1) + " jumps " + juce::String (step, 1) + " dB above " + name (l));
    }
    return out;
}
} // namespace emk
