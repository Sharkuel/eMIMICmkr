#include "BatchRecorder.h"

namespace emk
{
juce::Array<int> BatchRecorder::velocities (int count, int softest, int hardest)
{
    softest = juce::jlimit (1, 127, softest);
    hardest = juce::jlimit (softest, 127, hardest);
    juce::Array<int> out;
    for (int i = 0; i < count; ++i)
        out.add (count == 1 ? hardest
                            : (int) std::lround (hardest - (double) i * (hardest - softest) / (count - 1)));
    return out;
}

juce::String BatchRecorder::safeName (const juce::String& s)
{
    return s.replace (" - ", " ").removeCharacters ("/\\:*?\"<>|").trim();
}

juce::File BatchRecorder::fileFor (const juce::File& root, const juce::String& kitName, const KitZone& zone,
                                   int hitIndex, int velocity)
{
    const auto kit = safeName (kitName.isNotEmpty() ? kitName : juce::String ("Kit"));
    const auto piece = safeName (kit + " " + zone.piece);
    // The zone label may not contain '-' at all: wav2mci reads it as "[^-]+".
    const auto label = safeName (zone.label).replaceCharacter ('-', ' ');
    const auto name = piece + " - " + label + " - " + juce::String (hitIndex).paddedLeft ('0', 3)
                      + "v" + juce::String (velocity) + ".wav";
    return root.getChildFile (kit).getChildFile (piece).getChildFile (label).getChildFile (name);
}

juce::Array<int> BatchRecorder::findSoundingNotes (juce::AudioPluginInstance& plugin, HitRenderer::Settings settings,
                                                   std::function<bool (int)> onProgress)
{
    juce::Array<int> found;
    HitRenderer renderer (plugin, settings);
    for (int note = 0; note < 128; ++note)
    {
        if (! onProgress (note))
            break;
        if (renderer.renderHit (note, 100).getNumSamples() > 0)
            found.add (note);
    }
    return found;
}

bool BatchRecorder::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

juce::String BatchRecorder::record (juce::AudioPluginInstance& plugin, const KitMap& map, const Settings& settings,
                                    const juce::File& root, std::function<bool (const Progress&)> onProgress)
{
    const auto vels = velocities (settings.hitsPerZone, settings.softestVelocity, settings.hardestVelocity);
    juce::Array<KitZone> zones;
    for (const auto& z : map.zones)
        if (z.enabled)
            zones.add (z);

    Progress progress;
    progress.total = zones.size() * vels.size();
    int written = 0, clipped = 0;
    juce::StringArray silent, failed;

    HitRenderer renderer (plugin, settings.render);
    for (const auto& zone : zones)
    {
        int silentHits = 0;
        for (int i = 0; i < vels.size(); ++i)
        {
            progress.current = zone.piece + " - " + zone.label + " v" + juce::String (vels[i]);
            if (! onProgress (progress))
                return "Stopped after " + juce::String (written) + " hits.";

            auto hit = renderer.renderHit (zone.note, vels[i], zone.cc4);
            ++progress.done;
            if (hit.getNumSamples() == 0)
            {
                ++silentHits;
                continue;
            }
            if (hit.getMagnitude (0, 0, hit.getNumSamples()) > 1.0f)
                ++clipped;
            const auto file = fileFor (root, map.kitName, zone, i + 1, vels[i]);
            if (writeWav (file, hit, settings.render.sampleRate))
                ++written;
            else
                failed.addIfNotAlreadyThere (file.getParentDirectory().getFullPathName());
        }
        if (silentHits == vels.size())
            silent.add (zone.piece + " " + zone.label + " (note " + juce::String (zone.note) + ")");
    }

    juce::String report = "Recorded " + juce::String (written) + " hits to "
                          + root.getChildFile (safeName (map.kitName)).getFullPathName() + ".";
    if (! silent.isEmpty())
        report << "\nNo sound from: " << silent.joinIntoString (", ") << ".";
    if (clipped > 0)
        report << "\n" << clipped << " hits went over full scale; turn the plugin's master down a little.";
    if (! failed.isEmpty())
        report << "\nCould not write to: " << failed.joinIntoString (", ") << ".";
    return report;
}
} // namespace emk
