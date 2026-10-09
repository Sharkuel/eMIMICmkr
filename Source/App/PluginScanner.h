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

    // A plugin still opening after this is skipped. Generous: a Windows plugin through yabridge
    // can take minutes the first time Wine starts its prefix (the Skip window can end it sooner).
    static constexpr int timeoutSeconds = 300;
    static constexpr int skipOfferSeconds = 5;  // after this, a small window offers "Skip"
};
