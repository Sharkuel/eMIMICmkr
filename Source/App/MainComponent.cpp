#include "MainComponent.h"
#include "../Core/HitRenderer.h"

namespace
{
juce::File outputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("eMIMICmkr");
}

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setVisible (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};
} // namespace

MainComponent::MainComponent()
{
    formats.addDefaultFormats();

    settingsOptions.applicationName = "eMIMICmkr";
    settingsOptions.filenameSuffix = ".settings";
    settingsOptions.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions);
    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    for (auto* b : { &scanButton, &loadButton, &showButton, &renderButton })
        addAndMakeVisible (b);
    addAndMakeVisible (pluginChoice);
    addAndMakeVisible (status);

    scanButton.onClick = [this] { scanPlugins(); };
    loadButton.onClick = [this] { loadSelectedPlugin(); };
    showButton.onClick = [this] { showPluginWindow(); };
    renderButton.onClick = [this] { renderTestHit(); };

    refreshPluginChoices();
    setStatus ("Find your drum plugins to start.");
    setSize (640, 220);
}

MainComponent::~MainComponent()
{
    pluginWindow = nullptr;
    plugin = nullptr;
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto row = area.removeFromTop (32);
    scanButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    loadButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    pluginChoice.setBounds (row);

    area.removeFromTop (12);
    row = area.removeFromTop (32);
    showButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    renderButton.setBounds (row.removeFromLeft (140));

    area.removeFromTop (12);
    status.setBounds (area);
}

void MainComponent::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

void MainComponent::refreshPluginChoices()
{
    pluginChoice.clear();
    int id = 1;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            pluginChoice.addItem (d.name + " (" + d.pluginFormatName + ")", id++);
}

void MainComponent::scanPlugins()
{
    auto* list = new juce::PluginListComponent (formats, knownPlugins, {}, settings.get(), true);
    list->setSize (700, 500);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Find plugins";
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    setStatus ("Scan your plugin folders, then close the window and pick a plugin.");
}

void MainComponent::loadSelectedPlugin()
{
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    refreshPluginChoices();

    juce::Array<juce::PluginDescription> instruments;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            instruments.add (d);

    const int index = pluginChoice.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, instruments.size()))
    {
        setStatus ("Pick a plugin first.");
        return;
    }

    pluginWindow = nullptr;
    plugin = nullptr;
    juce::String error;
    plugin = formats.createPluginInstance (instruments[index], 48000.0, 512, error);
    setStatus (plugin != nullptr ? "Loaded " + plugin->getName() + ". Load a kit in the plugin window."
                                 : "Could not load the plugin: " + error);
    if (plugin != nullptr)
        showPluginWindow();
}

void MainComponent::showPluginWindow()
{
    if (plugin == nullptr)
        return;
    if (pluginWindow == nullptr)
        if (auto* editor = plugin->createEditorIfNeeded())
            pluginWindow = std::make_unique<PluginWindow> (editor, plugin->getName());
    if (pluginWindow != nullptr)
        pluginWindow->setVisible (true);
}

void MainComponent::renderTestHit()
{
    if (plugin == nullptr)
    {
        setStatus ("Load a plugin first.");
        return;
    }

    const auto& zone = kitMap.zones.getReference (0); // the kick
    juce::AudioBuffer<float> hit;
    {
        emk::HitRenderer renderer (*plugin, {});
        hit = renderer.renderHit (zone.note, 100);
    }
    plugin->prepareToPlay (48000.0, 512);

    if (hit.getNumSamples() == 0)
    {
        setStatus ("The plugin stayed silent on note " + juce::String (zone.note) + ".");
        return;
    }

    auto file = outputFolder().getChildFile ("test-hit.wav");
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (48000.0)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples()))
    {
        setStatus ("Could not write " + file.getFullPathName());
        return;
    }
    setStatus ("Saved " + juce::String (hit.getNumSamples() / 48000.0, 2) + " s hit to " + file.getFullPathName());
}
