#include "RecordQueue.h"
#include "BatchRecorder.h"

namespace emk
{
juce::Result RecordQueue::add (const juce::File& root, const juce::String& pluginName, const juce::MemoryBlock& state,
                               const KitMap& map)
{
    if (map.kitName.trim().isEmpty())
        return juce::Result::fail ("Give the kit a name first; it names the folders and files.");
    const auto queue = folderIn (root);
    int last = 0;
    for (const auto& f : queue.findChildFiles (juce::File::findDirectories, false))
        last = std::max (last, f.getFileName().getIntValue());
    const auto folder = queue.getChildFile (juce::String (last + 1).paddedLeft ('0', 2) + " "
                                            + BatchRecorder::safeName (map.kitName));
    if (! folder.createDirectory())
        return juce::Result::fail ("Could not make " + folder.getFullPathName() + ".");
    const bool ok = folder.getChildFile ("plugin.bin").replaceWithData (state.getData(), state.getSize())
                    && folder.getChildFile ("map.csv").replaceWithText (map.toCsv())
                    && folder.getChildFile ("queue.txt").replaceWithText ("kit=" + map.kitName + "\nplugin=" + pluginName + "\n",
                                                                          false, false, "\n");
    if (! ok)
    {
        folder.deleteRecursively();
        return juce::Result::fail ("Could not write the queue in " + queue.getFullPathName() + ".");
    }
    return juce::Result::ok();
}

std::vector<RecordQueue::Entry> RecordQueue::list (const juce::File& root)
{
    auto folders = folderIn (root).findChildFiles (juce::File::findDirectories, false);
    std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().getIntValue() < b.getFileName().getIntValue();
    });
    std::vector<Entry> out;
    for (const auto& f : folders)
    {
        Entry e;
        e.folder = f;
        juce::StringPairArray info;
        for (const auto& line : juce::StringArray::fromLines (f.getChildFile ("queue.txt").loadFileAsString()))
            if (line.contains ("="))
                info.set (line.upToFirstOccurrenceOf ("=", false, false), line.fromFirstOccurrenceOf ("=", false, false));
        e.kitName = info["kit"];
        e.pluginName = info["plugin"];
        juce::String error;
        e.map = KitMap::fromCsv (f.getChildFile ("map.csv").loadFileAsString(), error);
        e.map.kitName = e.kitName;
        if (e.kitName.isEmpty() || error.isNotEmpty() || ! f.getChildFile ("plugin.bin").loadFileAsData (e.state))
            continue;
        out.push_back (std::move (e));
    }
    return out;
}

bool RecordQueue::waitForSound (juce::AudioPluginInstance& plugin, const KitMap& map, const HitRenderer::Settings& settings,
                                double timeoutSeconds, std::function<bool()> shouldStop, double retrySeconds)
{
    const KitZone* first = nullptr;
    for (const auto& z : map.zones)
        if (z.enabled)
        {
            first = &z;
            break;
        }
    if (first == nullptr)
        return false;
    HitRenderer renderer (plugin, settings);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (;;)
    {
        if (renderer.renderHit (first->note, 100, first->cc4).getNumSamples() > 0)
            return true;
        if (shouldStop() || juce::Time::getMillisecondCounterHiRes() - start > timeoutSeconds * 1000.0)
            return false;
        juce::Thread::sleep ((int) (retrySeconds * 1000.0));
    }
}
} // namespace emk
