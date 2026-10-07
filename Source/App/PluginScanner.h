#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Scans each plugin file in a child copy of the app, so a plugin that crashes while
    being opened only takes the child down. The plugin is then skipped (blacklisted). */
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    /** The child's side: "--scan-plugin <format> <file> <output.xml>" on the command line.
        Returns true when the command line asked for a scan (which then ends the process). */
    static bool runChildScanIfRequested (const juce::String& commandLine);
};
