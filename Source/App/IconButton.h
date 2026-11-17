#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** A square text button with a small white icon at its right end. */
class IconButton : public juce::TextButton
{
public:
    enum Icon { folder };

    IconButton (const juce::String& text, Icon icon) : juce::TextButton (text)
    {
        if (auto xml = juce::XmlDocument::parse (svgFor (icon)))
            drawable = juce::Drawable::createFromSVG (*xml);
    }

    /** With a picture instead: white on transparent, drawn like the built-in icons. */
    IconButton (const juce::String& text, const juce::Image& picture) : juce::TextButton (text)
    {
        auto image = std::make_unique<juce::DrawableImage>();
        image->setImage (picture);
        drawable = std::move (image);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& look = getLookAndFeel();
        look.drawButtonBackground (g, *this, findColour (buttonColourId), highlighted, down);

        auto area = getLocalBounds().reduced (6, 4);
        // A wide picture gets a wider spot, so it isn't shrunk to fit a square.
        const auto aspect = drawable != nullptr && drawable->getDrawableBounds().getHeight() > 0
                                ? std::max (1.0f, drawable->getDrawableBounds().getWidth() / drawable->getDrawableBounds().getHeight())
                                : 1.0f;
        // At most 22 px high, so a tall two-line button keeps room for its text.
        const auto iconH = std::min (area.getHeight(), 22);
        const auto iconArea = area.removeFromRight (juce::roundToInt ((float) iconH * aspect)).toFloat();
        if (drawable != nullptr)
            drawable->drawWithin (g, iconArea, juce::RectanglePlacement::centred, isEnabled() ? 1.0f : 0.4f);

        g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
        const auto lines = juce::StringArray::fromLines (getButtonText()).size();
        auto font = look.getTextButtonFont (*this, getHeight());
        if (lines > 1)
            font = font.withHeight (std::min (font.getHeight(), 14.0f));   // two lines beside an icon
        g.setFont (font);
        g.drawFittedText (getButtonText(), area.withTrimmedRight (4), juce::Justification::centred, lines, 0.8f);
    }

private:
    std::unique_ptr<juce::Drawable> drawable;

    static const char* svgFor (Icon icon)
    {
        juce::ignoreUnused (icon);   // the folder is the only built-in one
        return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g fill="#ffffff">
<path d="M2 5.5C2 4.7 2.7 4 3.5 4H9l2 2.5h8.5c.8 0 1.5.7 1.5 1.5V9H6.2c-.7 0-1.3.4-1.5 1.1L2 17.5z"/>
<path d="M5.6 10.5h17.2l-3.1 8.4c-.2.7-.8 1.1-1.5 1.1H2.6z"/></g></svg>)";
    }
};
