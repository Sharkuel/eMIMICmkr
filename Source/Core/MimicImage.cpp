#include "MimicImage.h"

namespace emk
{
juce::MemoryBlock MimicImage::png (const juce::Image& source)
{
    if (! source.isValid())
        return {};
    juce::Image out (juce::Image::RGB, width, height, true);
    {
        juce::Graphics g (out);
        g.fillAll (juce::Colours::black);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        // Shrinking a lot in one go skips pixels: halve first until close to the size.
        auto image = source;
        while (image.getWidth() > width * 3 && image.getHeight() > height * 3)
            image = image.rescaled (image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::highResamplingQuality);
        g.drawImageWithin (image, 0, 0, width, height, juce::RectanglePlacement::centred);
    }
    juce::MemoryOutputStream stream;
    juce::PNGImageFormat format;
    if (! format.writeImageToStream (out, stream))
        return {};
    return stream.getMemoryBlock();
}

juce::Image MimicImage::load (const juce::MemoryBlock& data)
{
    return data.getSize() > 0 ? juce::ImageFileFormat::loadFrom (data.getData(), data.getSize()) : juce::Image();
}
} // namespace emk
