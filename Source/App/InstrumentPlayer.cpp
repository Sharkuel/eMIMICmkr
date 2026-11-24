#include "InstrumentPlayer.h"

InstrumentPlayer::InstrumentPlayer (emk::mci::MciFile f, Play p) : player (std::move (f)), play (std::move (p))
{
    for (auto* c : std::initializer_list<juce::Component*> { &title, &artLabel, &articulation, &velocityLabel, &velocity,
                                                             &hitButton, &layersButton, &layers })
        addAndMakeVisible (c);
    const auto& file = player.file();
    title.setText (file.getString ("INST0insnam") + "  (" + file.getString ("INST0libnam") + ")  "
                       + file.getString ("INST0instyp"),
                   juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));

    int id = 1;
    for (const auto& a : player.articulations())
        articulation.addItem (a.name.isNotEmpty() ? a.name : "Articulation " + juce::String (id), id), ++id;
    articulation.setSelectedId (1, juce::dontSendNotification);
    articulation.onChange = [this] { showLayers(); };

    velocity.setRange (1, 127, 1);
    velocity.setValue (100, juce::dontSendNotification);
    velocity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
    velocity.setTooltip ("How hard the pad is hit (1 to 127).");
    hitButton.onClick = [this] { hit(); };
    hitButton.setTooltip ("Plays one hit at this velocity. Each hit takes the layer's next round robin, as the Mimic does.");
    layersButton.onClick = [this] { playLayers(); };
    layersButton.setTooltip ("Plays each layer in turn, softest first, at the velocity where it starts.");

    layers.setMultiLine (true);
    layers.setReadOnly (true);
    layers.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    showLayers();
    setSize (560, 420);
}

void InstrumentPlayer::resized()
{
    auto r = getLocalBounds().reduced (12);
    title.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    artLabel.setBounds (row.removeFromLeft (100));
    articulation.setBounds (row);
    r.removeFromTop (6);
    row = r.removeFromTop (28);
    velocityLabel.setBounds (row.removeFromLeft (100));
    velocity.setBounds (row);
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    hitButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (8);
    layersButton.setBounds (row.removeFromLeft (150));
    r.removeFromTop (8);
    layers.setBounds (r);
}

void InstrumentPlayer::hit()
{
    const auto out = player.hit (articulation.getSelectedId() - 1, (int) velocity.getValue());
    if (out.getNumSamples() > 0)
        play (out);
}

void InstrumentPlayer::playLayers()
{
    const int a = articulation.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (a, (int) player.articulations().size()))
        return;
    const int gap = 44100 * 6 / 10;   // a hit every 0.6 s, each left to ring under the next
    std::vector<juce::AudioBuffer<float>> hits;
    for (const auto& layer : player.articulations()[(size_t) a].layers)
        hits.push_back (player.hit (a, std::max (1, layer.minVelocity)));
    int length = 0;
    for (size_t i = 0; i < hits.size(); ++i)
        length = std::max (length, (int) i * gap + hits[i].getNumSamples());
    juce::AudioBuffer<float> out (2, length);
    out.clear();
    for (size_t i = 0; i < hits.size(); ++i)
        for (int c = 0; c < 2; ++c)
            if (hits[i].getNumSamples() > 0)
                out.addFrom (c, (int) i * gap, hits[i], c, 0, hits[i].getNumSamples());
    if (length > 0)
        play (out);
}

void InstrumentPlayer::showLayers()
{
    const int a = articulation.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (a, (int) player.articulations().size()))
        return;
    const auto& art = player.articulations()[(size_t) a];
    const auto levels = player.layerLevels (a);
    juce::String text;
    if (art.layers.empty())
        text = "This articulation has no samples.\n";
    for (size_t l = 0; l < art.layers.size(); ++l)
        text << "Layer " << juce::String ((int) l + 1).paddedLeft (' ', 2) << "  from velocity "
             << juce::String (art.layers[l].minVelocity).paddedLeft (' ', 3) << "  "
             << juce::String ((int) art.layers[l].samples.size()) << " round robin"
             << (art.layers[l].samples.size() == 1 ? "  " : "s ") << "  "
             << juce::String (levels[l], 1).paddedLeft (' ', 6) << " dB\n";
    const auto problems = player.layerProblems (a);
    text << "\n" << (problems.isEmpty() ? juce::String ("Levels rise evenly from soft to hard.")
                                        : "Check these:\n" + problems.joinIntoString ("\n"));
    layers.setText (text, false);
}

InstrumentPlayerWindow::InstrumentPlayerWindow (const juce::File& file, emk::mci::MciFile f, InstrumentPlayer::Play play)
    : DocumentWindow ("Play " + file.getFileName(), juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                        .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new InstrumentPlayer (std::move (f), std::move (play)), true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
