#pragma once
#include "HitRenderer.h"
#include "KitMap.h"

namespace emk
{
/** Kits waiting to be recorded one after another, say overnight: each is the plugin's whole
    state (Superior Drummer 3 and BFD3 keep the loaded kit in it) and the note map, kept in

        <root>/Queue/<NN> <kit>/plugin.bin, map.csv, queue.txt (kit and plugin names)

    so the queue outlives the app. A kit leaves the queue once it is recorded and converted. */
struct RecordQueue
{
    struct Entry
    {
        juce::File folder;
        juce::String kitName, pluginName;
        juce::MemoryBlock state;
        KitMap map;
    };

    static juce::File folderIn (const juce::File& root) { return root.getChildFile ("Queue"); }

    /** Adds a kit at the end of the queue; the map's kitName names it. */
    static juce::Result add (const juce::File& root, const juce::String& pluginName, const juce::MemoryBlock& state,
                             const KitMap&);
    /** In the order they were added. A folder that can't be read is left out. */
    static std::vector<Entry> list (const juce::File& root);
    static bool remove (const Entry& e) { return e.folder.deleteRecursively(); }

    /** A preset loading its samples plays nothing yet: renders the map's first ticked zone until
        it sounds, every `retrySeconds`, for up to `timeoutSeconds`. False when it stays silent
        or shouldStop() says so. */
    static bool waitForSound (juce::AudioPluginInstance&, const KitMap&, const HitRenderer::Settings&,
                              double timeoutSeconds, std::function<bool()> shouldStop, double retrySeconds = 2.0);
};
} // namespace emk
