#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/KitMap.h"

/** First working screen: find drum plugins, load one, show it, render a test hit. */
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    void scanPlugins();
    void refreshPluginChoices();
    void loadSelectedPlugin();
    void showPluginWindow();
    void renderTestHit();
    void setStatus (const juce::String& text);

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::PropertiesFile::Options settingsOptions;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::DocumentWindow> pluginWindow;
    emk::KitMap kitMap = emk::KitMap::generalMidi();

    juce::TextButton scanButton { "Find plugins" }, loadButton { "Load plugin" },
        showButton { "Show plugin" }, renderButton { "Render test hit" };
    juce::ComboBox pluginChoice;
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
