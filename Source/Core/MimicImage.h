#pragma once
#include <juce_graphics/juce_graphics.h>

namespace emk
{
/** The picture the Mimic shows for an instrument: a 94x63 PNG. */
struct MimicImage
{
    static constexpr int width = 94, height = 63;

    /** Any picture, scaled to fit 94x63 whole and centred on black, as a PNG. Empty for an
        invalid image. */
    static juce::MemoryBlock png (const juce::Image&);

    static juce::Image load (const juce::MemoryBlock&);
};
} // namespace emk
