#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** A square text button with a small white icon at its right end. */
class IconButton : public juce::TextButton
{
public:
    enum Icon { folder, drumKit };

    IconButton (const juce::String& text, Icon icon) : juce::TextButton (text)
    {
        if (auto xml = juce::XmlDocument::parse (svgFor (icon)))
            drawable = juce::Drawable::createFromSVG (*xml);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& look = getLookAndFeel();
        look.drawButtonBackground (g, *this, findColour (buttonColourId), highlighted, down);

        auto area = getLocalBounds().reduced (6, 4);
        const auto iconArea = area.removeFromRight (area.getHeight()).toFloat();
        if (drawable != nullptr)
            drawable->drawWithin (g, iconArea, juce::RectanglePlacement::centred, isEnabled() ? 1.0f : 0.4f);

        g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
        g.setFont (look.getTextButtonFont (*this, getHeight()));
        g.drawFittedText (getButtonText(), area.withTrimmedRight (4), juce::Justification::centred, 1);
    }

private:
    std::unique_ptr<juce::Drawable> drawable;

    static const char* svgFor (Icon icon)
    {
        if (icon == folder)
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g fill="#ffffff">
<path d="M2 5.5C2 4.7 2.7 4 3.5 4H9l2 2.5h8.5c.8 0 1.5.7 1.5 1.5V9H6.2c-.7 0-1.3.4-1.5 1.1L2 17.5z"/>
<path d="M5.6 10.5h17.2l-3.1 8.4c-.2.7-.8 1.1-1.5 1.1H2.6z"/></g></svg>)";

        // A small kit: crash on its stand, a tom on the bass drum, the bass drum head-on.
        return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g fill="none" stroke="#ffffff" stroke-width="1.6" stroke-linecap="round">
<path d="M1.5 4.2l7 -1.6"/><path d="M5 3.4V14"/>
<path d="M15.5 3.2l7 1.6"/><path d="M19 4V14"/>
<circle cx="12" cy="15.5" r="6.2"/><circle cx="12" cy="15.5" r="2.2"/>
<rect x="7.3" y="5.6" width="5.2" height="3.6" rx="0.6"/>
<path d="M2.8 21.6l2.2 -7.6 2.2 7.6"/><path d="M16.8 21.6l2.2 -7.6 2.2 7.6"/></g></svg>)";
    }
};
