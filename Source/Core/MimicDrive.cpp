#include "MimicDrive.h"

namespace emk::MimicDrive
{
bool isDrive (const juce::File& f)
{
    return f != juce::File() && f.getChildFile ("Instruments").isDirectory() && f.getChildFile ("Kits").isDirectory();
}

juce::File find (const juce::File& remembered)
{
    if (isDrive (remembered))
        return remembered;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Mimic");
    return isDrive (home) ? home : juce::File();
}

static juce::String legal (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim());
}

juce::File instrumentsFor (const juce::File& drive, const juce::String& kitName)
{
    return drive.getChildFile ("Instruments").getChildFile ("eMIMICmkr").getChildFile (legal (kitName));
}

juce::File kitFileFor (const juce::File& drive, const juce::String& kitName)
{
    return drive.getChildFile ("Kits").getChildFile ("[U] " + legal (kitName) + ".kit");
}

/** Same bytes, read cheaply over the network: small files whole; large ones by size and their
    last 64 KB, which for a .mci ends with the MD5 of the whole file. */
static bool sameBytes (const juce::File& a, const juce::File& b)
{
    const auto size = a.getSize();
    if (! b.existsAsFile() || b.getSize() != size)
        return false;
    constexpr juce::int64 whole = 1 << 20, tail = 1 << 16;
    const auto from = size <= whole ? 0 : size - tail;
    juce::FileInputStream x (a), y (b);
    if (! x.openedOk() || ! y.openedOk() || ! x.setPosition (from) || ! y.setPosition (from))
        return false;
    juce::MemoryBlock bx, by;
    x.readIntoMemoryBlock (bx);
    y.readIntoMemoryBlock (by);
    return bx == by;
}

Report send (const juce::Array<juce::File>& files, const juce::File& folder, const juce::StringArray& names)
{
    Report report;
    if (! folder.createDirectory())
    {
        for (const auto& f : files)
            report.failed.add (f.getFileName() + " (cannot make " + folder.getFullPathName() + ")");
        return report;
    }
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& source = files.getReference (i);
        const auto name = names[i].isNotEmpty() ? names[i] : source.getFileName();
        const auto target = folder.getChildFile (name);
        if (sameBytes (source, target))
        {
            report.unchanged.add (name);
            continue;
        }
        const auto part = folder.getChildFile ("." + name + ".part");
        const bool ok = source.copyFileTo (part) && sameBytes (source, part) && part.moveFileTo (target);
        if (! ok)
        {
            part.deleteFile();
            report.failed.add (name);
            continue;
        }
        report.copied.add (name);
    }
    return report;
}

juce::String Report::toString() const
{
    juce::StringArray parts;
    if (! copied.isEmpty())
        parts.add (juce::String (copied.size()) + " sent");
    if (! unchanged.isEmpty())
        parts.add (juce::String (unchanged.size()) + " already there");
    if (! failed.isEmpty())
        parts.add ("could not send " + failed.joinIntoString (", "));
    return parts.joinIntoString ("; ");
}
} // namespace emk::MimicDrive
