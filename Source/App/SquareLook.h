#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** JUCE's dark look with square corners on buttons and drop-down lists. */
class SquareLook : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour.withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
                          .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);
        if (down || highlighted)
            colour = colour.contrasting (down ? 0.2f : 0.05f);
        g.setColour (colour);
        g.fillRect (bounds);
        g.setColour (button.findColour (juce::ComboBox::outlineColourId));
        g.drawRect (bounds, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const juce::Rectangle<int> bounds (0, 0, width, height);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (bounds);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRect (bounds.toFloat().reduced (0.5f), 1.0f);

        const juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);
        juce::Path path;
        path.startNewSubPath ((float) arrowZone.getX() + 3.0f, (float) arrowZone.getCentreY() - 2.0f);
        path.lineTo ((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
        path.lineTo ((float) arrowZone.getRight() - 3.0f, (float) arrowZone.getCentreY() - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    /** A square frame with its title centred in the top line, like "MIDI" over the note buttons. */
    void drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent& group) override
    {
        const juce::Font font (juce::FontOptions (15.0f));
        const float top = font.getHeight() * 0.5f;
        const auto frame = juce::Rectangle<float> (0.0f, top, (float) width, (float) height - top).reduced (0.5f);
        const float textW = text.isEmpty() ? 0.0f
                                           : juce::GlyphArrangement::getStringWidth (font, text) + 8.0f;
        const float textX = frame.getCentreX() - textW * 0.5f;

        g.setColour (group.findColour (juce::GroupComponent::outlineColourId));
        g.drawLine (frame.getX(), frame.getY(), textX, frame.getY());
        g.drawLine (textX + textW, frame.getY(), frame.getRight(), frame.getY());
        g.drawLine (frame.getRight(), frame.getY(), frame.getRight(), frame.getBottom());
        g.drawLine (frame.getRight(), frame.getBottom(), frame.getX(), frame.getBottom());
        g.drawLine (frame.getX(), frame.getBottom(), frame.getX(), frame.getY());

        g.setColour (group.findColour (juce::GroupComponent::textColourId));
        g.setFont (font);
        g.drawText (text, juce::Rectangle<float> (textX, 0.0f, textW, font.getHeight()), juce::Justification::centred, false);
    }
};
