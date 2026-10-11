#include "BatchRecorder.h"
#include "WavToMci.h"
#include <set>

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
                                                   .withBitsPerSample (32)   // float: a hit past full scale isn't clipped
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

juce::String BatchRecorder::record (juce::AudioPluginInstance& plugin, const KitMap& map, const Settings& settings,
                                    const juce::File& root, std::function<bool (const Progress&)> onProgress)
{
    juce::Array<KitZone> zones;
    juce::Array<juce::Array<int>> zoneVelocities;
    Progress progress;
    for (int i = 0; i < map.zones.size(); ++i)
        if (map.zones[i].enabled)
        {
            const int hits = map.hitsFor (i, settings.hitsPerZone, settings.fitMimicLimits);
            zones.add (map.zones[i]);
            zoneVelocities.add (velocities (hits, settings.softestVelocity, settings.hardestVelocity));
            progress.total += hits;
        }

    int written = 0;
    juce::StringArray silent, failed;

    HitRenderer renderer (plugin, settings.render);
    for (int z = 0; z < zones.size(); ++z)
    {
        const auto& zone = zones.getReference (z);
        const auto& vels = zoneVelocities.getReference (z);
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
            const auto file = fileFor (root, map.kitName, zone, i + 1, vels[i]);
            if (writeWav (file, hit, settings.render.sampleRate))
                ++written;
            else
                failed.addIfNotAlreadyThere (file.getParentDirectory().getFullPathName());
        }
        if (silentHits == vels.size())
            silent.add (zone.piece + " " + zone.label + " (note " + juce::String (zone.note) + ")");
    }

    // What the Mimic shows for each piece, next to its zone folders for the converter.
    std::set<juce::String> described;
    for (const auto& zone : zones)
        if (described.insert (zone.piece).second)
        {
            WavToMci::PieceInfo info;
            for (const auto& z : zones)
                if (z.piece == zone.piece)
                {
                    if (info.name.isEmpty()) info.name = z.name.trim();
                    if (info.library.isEmpty()) info.library = z.library.trim();
                    if (info.image.getSize() == 0) info.image = z.image;
                }
            if (info.library.isEmpty())
                info.library = map.kitName.trim();
            const auto folder = fileFor (root, map.kitName, zone, 1, 1).getParentDirectory().getParentDirectory();
            if (! folder.isDirectory())
                continue;
            folder.getChildFile (WavToMci::infoFile).replaceWithText (WavToMci::infoText (info));
            const auto png = folder.getChildFile (WavToMci::imageFile);
            if (info.image.getSize() > 0)
                png.replaceWithData (info.image.getData(), info.image.getSize());
            else
                png.deleteFile();
        }

    juce::String report = "Recorded " + juce::String (written) + " hits to "
                          + root.getChildFile (safeName (map.kitName)).getFullPathName() + ".";
    if (! silent.isEmpty())
        report << "\nNo sound from: " << silent.joinIntoString (", ") << ".";
    if (! failed.isEmpty())
        report << "\nCould not write to: " << failed.joinIntoString (", ") << ".";
    return report;
}
} // namespace emk
