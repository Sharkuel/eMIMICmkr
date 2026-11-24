#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/MciPlayer.h"

/** Plays a built instrument (.mci) the way the Mimic does: pick an articulation and a
    velocity and hit it, round robins in turn, or play every layer soft to hard. Lists each
    layer's level and flags one louder than the layer above it, or a big jump. */
class InstrumentPlayer : public juce::Component
{
public:
    using Play = std::function<void (const juce::AudioBuffer<float>&)>;
    InstrumentPlayer (emk::mci::MciFile, Play);
    void resized() override;

private:
    void hit();
    void playLayers();
    void showLayers();

    emk::MciPlayer player;
    Play play;
    juce::Label title, artLabel { {}, "Articulation" }, velocityLabel { {}, "Velocity" };
    juce::ComboBox articulation;
    juce::Slider velocity;
    juce::TextButton hitButton { "Hit" }, layersButton { "Play every layer" };
    juce::TextEditor layers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPlayer)
};

class InstrumentPlayerWindow : public juce::DocumentWindow
{
public:
    InstrumentPlayerWindow (const juce::File&, emk::mci::MciFile, InstrumentPlayer::Play);
    void closeButtonPressed() override { setVisible (false); }
};
