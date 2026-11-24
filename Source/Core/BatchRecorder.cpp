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
    // Through a temporary file, so a recording stopped mid-write leaves no cut-off hit to keep.
    file.getParentDirectory().createDirectory();
    const auto temp = file.getSiblingFile ("." + file.getFileName() + ".part");
    temp.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream());
        if (stream == nullptr)
            return false;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (32)   // float: a hit past full scale isn't clipped
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        {
            writer.reset();
            temp.deleteFile();
            return false;
        }
    }
    file.deleteFile();
    return temp.moveFileTo (file);
}

juce::String BatchRecorder::zoneInfo (const KitZone& zone, const juce::Array<int>& vels, const Settings& settings,
                                      const juce::String& pluginName)
{
    juce::StringArray v;
    for (auto x : vels)
        v.add (juce::String (x));
    const auto& m = settings.render.mics;
    juce::String text;
    text << "note: " << zone.note << "\n"
         << "cc4: " << zone.cc4 << "\n"
         << "velocities: " << v.joinIntoString (",") << "\n"
         << "mics: " << (m.enabled() ? "close " + juce::String (m.close) + ", overheads " + juce::String (m.overheads)
                                           + ", room " + juce::String (m.room)
                                     : juce::String ("one mix")) << "\n"
         << "longest: " << settings.render.maxSeconds << " s\n"
         << "rate: " << settings.render.sampleRate << "\n"
         << "plugin: " << pluginName << "\n";
    return text;
}

namespace
{
struct ZoneJob
{
    KitZone zone;
    juce::Array<int> velocities;
    juce::File folder;
    juce::String info;
};

std::vector<ZoneJob> jobsFor (const KitMap& map, const BatchRecorder::Settings& settings, const juce::File& root,
                              const juce::String& pluginName)
{
    std::vector<ZoneJob> jobs;
    for (int i = 0; i < map.zones.size(); ++i)
        if (map.zones[i].enabled)
        {
            ZoneJob j;
            j.zone = map.zones[i];
            j.velocities = BatchRecorder::velocities (map.hitsFor (i, settings.hitsPerZone, settings.fitMimicLimits, settings.stockLimits),
                                                      settings.softestVelocity, settings.hardestVelocity);
            j.folder = BatchRecorder::fileFor (root, map.kitName, j.zone, 1, 1).getParentDirectory();
            j.info = BatchRecorder::zoneInfo (j.zone, j.velocities, settings, pluginName);
            jobs.push_back (std::move (j));
        }
    return jobs;
}

bool sameInfo (const ZoneJob& job)
{
    const auto file = job.folder.getChildFile (BatchRecorder::zoneInfoFile);
    return file.existsAsFile() && file.loadFileAsString() == job.info;
}

/** The zone's hit files: "<piece> - <label> - NNNvVV.wav". */
juce::Array<juce::File> hitFiles (const ZoneJob& job, const KitMap& map, const juce::File& root)
{
    const auto prefix = BatchRecorder::fileFor (root, map.kitName, job.zone, 1, 1).getFileName()
                            .upToLastOccurrenceOf (" - ", true, false);
    juce::Array<juce::File> out;
    for (const auto& f : job.folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV"))
        if (f.getFileName().startsWith (prefix))
            out.add (f);
    return out;
}
} // namespace

BatchRecorder::Plan BatchRecorder::plan (const KitMap& map, const Settings& settings, const juce::File& root,
                                         const juce::String& pluginName)
{
    Plan p;
    for (const auto& job : jobsFor (map, settings, root, pluginName))
    {
        if (! job.folder.isDirectory())
        {
            ++p.zonesNew;
            p.toRecord += job.velocities.size();
            continue;
        }
        if (! sameInfo (job))
        {
            ++p.zonesChanged;
            p.toRecord += job.velocities.size();
            continue;
        }
        int kept = 0;
        for (int i = 0; i < job.velocities.size(); ++i)
            kept += fileFor (root, map.kitName, job.zone, i + 1, job.velocities[i]).existsAsFile() ? 1 : 0;
        p.kept += kept;
        p.toRecord += job.velocities.size() - kept;
        ++(kept == job.velocities.size() ? p.zonesKept : p.zonesChanged);
    }
    return p;
}

juce::String BatchRecorder::record (juce::AudioPluginInstance& plugin, const KitMap& map, const Settings& settings,
                                    const juce::File& root, std::function<bool (const Progress&)> onProgress)
{
    const auto jobs = jobsFor (map, settings, root, plugin.getName());
    juce::Array<KitZone> zones;
    Progress progress;
    for (const auto& job : jobs)
    {
        zones.add (job.zone);
        progress.total += job.velocities.size();
    }

    int written = 0, kept = 0;
    juce::StringArray silent, failed;

    HitRenderer renderer (plugin, settings.render);
    for (const auto& job : jobs)
    {
        const auto& zone = job.zone;
        const auto& vels = job.velocities;
        const bool keep = settings.keepExisting && sameInfo (job);
        // Old hits this recording doesn't make (another count, another velocity) would end up in
        // the instrument: take them out. With keep, those it makes again stay until replaced.
        juce::Array<juce::File> wanted;
        for (int i = 0; i < vels.size(); ++i)
            wanted.add (fileFor (root, map.kitName, zone, i + 1, vels[i]));
        for (const auto& f : hitFiles (job, map, root))
            if (! keep || ! wanted.contains (f))
                f.deleteFile();
        // Written first, so a recording that stops halfway keeps what it made.
        job.folder.createDirectory();
        job.folder.getChildFile (zoneInfoFile).replaceWithText (job.info, false, false, "\n");

        int silentHits = 0;
        for (int i = 0; i < vels.size(); ++i)
        {
            progress.current = zone.piece + " - " + zone.label + " v" + juce::String (vels[i]);
            if (! onProgress (progress))
                return "Stopped after " + juce::String (written) + " hits. Record kit again to finish it: the hits "
                       "already made are kept.";
            const auto& file = wanted.getReference (i);
            if (keep && file.existsAsFile())
            {
                ++progress.done;
                ++kept;
                continue;
            }

            auto hit = renderer.renderHit (zone.note, vels[i], zone.cc4);
            ++progress.done;
            if (hit.getNumSamples() == 0)
            {
                ++silentHits;
                continue;
            }
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
    if (kept > 0)
        report << " Kept " << kept << " recorded before.";
    if (! silent.isEmpty())
        report << "\nNo sound from: " << silent.joinIntoString (", ") << ".";
    if (! failed.isEmpty())
        report << "\nCould not write to: " << failed.joinIntoString (", ") << ".";
    return report;
}
} // namespace emk
