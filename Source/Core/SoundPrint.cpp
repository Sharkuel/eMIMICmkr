#include "SoundPrint.h"
#include <juce_dsp/juce_dsp.h>

namespace emk
{
namespace
{
constexpr int bands = 40;
constexpr double lowHz = 40.0, highHz = 16000.0;

float rmsDb (const float* x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return (float) (10.0 * std::log10 (sum / juce::jmax (1, to - from) + 1.0e-14));
}
} // namespace

SoundPrint SoundPrint::of (const float* mono, int numSamples, double rate)
{
    SoundPrint print;
    if (mono == nullptr || numSamples <= 0 || rate <= 0.0)
        return print;

    // Spectrum of the first ~90 ms, the attack that tells drums apart.
    const int order = rate > 60000.0 ? 13 : 12;
    juce::dsp::FFT fft (order);
    const int size = fft.getSize();
    std::vector<float> data ((size_t) size * 2, 0.0f);
    const int n = juce::jmin (size, numSamples);
    for (int i = 0; i < n; ++i)
        data[(size_t) i] = mono[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size));
    fft.performFrequencyOnlyForwardTransform (data.data(), true);

    const double binHz = rate / size, top = juce::jmin (highHz, rate * 0.5);
    for (int b = 0; b < bands; ++b)
    {
        const double from = lowHz * std::pow (top / lowHz, (double) b / bands);
        const double to = lowHz * std::pow (top / lowHz, (double) (b + 1) / bands);
        const int first = juce::jlimit (1, size / 2, (int) std::floor (from / binHz));
        const int last = juce::jlimit (first + 1, size / 2 + 1, (int) std::ceil (to / binHz));
        double energy = 0.0;
        for (int k = first; k < last; ++k)
            energy += (double) data[(size_t) k] * data[(size_t) k];
        print.features.push_back ((float) (10.0 * std::log10 (energy / (last - first) + 1.0e-14)));
    }
    float mean = 0.0f;
    for (auto f : print.features)
        mean += f;
    mean /= (float) bands;
    for (auto& f : print.features)
        f -= mean;   // a soft and a hard hit of the same drum differ mostly in level

    // How much quieter the next 200 ms are than the first 100 ms: a cymbal rings, a drum dies.
    const int split = juce::jmin (numSamples, (int) (0.1 * rate)), end = juce::jmin (numSamples, (int) (seconds * rate));
    print.features.push_back (end > split ? rmsDb (mono, split, end) - rmsDb (mono, 0, split) : -60.0f);
    return print;
}

float SoundPrint::distanceTo (const SoundPrint& other) const
{
    if (features.size() != other.features.size())
        return std::numeric_limits<float>::max();
    double sum = 0.0;
    for (size_t i = 0; i < features.size(); ++i)
    {
        const double d = juce::jlimit (-40.0, 40.0, (double) features[i] - other.features[i]);
        sum += d * d;
    }
    return (float) std::sqrt (sum / (double) features.size());
}

int closestNote (const std::map<int, SoundPrint>& prints, const SoundPrint& heard, float* ratio)
{
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max(), second = bestDistance;
    for (const auto& [note, print] : prints)
    {
        const float d = heard.distanceTo (print);
        if (d < bestDistance)
        {
            second = bestDistance;
            bestDistance = d;
            best = note;
        }
        else if (d < second)
        {
            second = d;
        }
    }
    if (ratio != nullptr)
        *ratio = prints.size() < 2 ? 0.0f : second / juce::jmax (1.0e-6f, bestDistance);
    return best;
}
} // namespace emk
