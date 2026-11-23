#pragma once
#include <juce_core/juce_core.h>

/** A Mimic running Mimic OS 2, seen as a folder on this computer.

    Mimic OS 2's mimic-share mounts the module over the USB Ethernet link as ~/Mimic, holding
    Instruments (/mnt/user/instruments) and Kits (/mnt/settings/kits). The Mimic finds an
    instrument in any folder below Instruments and a user kit as Kits/[U] <name>.kit, and picks
    up new files by itself about 10 s after a copy ends. Any folder holding Instruments and Kits
    works the same way, so a drive mounted another way (SSHFS-Win, macFUSE) can be picked once. */
namespace emk::MimicDrive
{
bool isDrive (const juce::File&);

/** `remembered` if it is a drive, else ~/Mimic if it is one, else File(). */
juce::File find (const juce::File& remembered);

/** Instruments/eMIMICmkr/<kit name>: where a kit's instruments go. */
juce::File instrumentsFor (const juce::File& drive, const juce::String& kitName);

/** Kits/[U] <kit name>.kit: where the Mimic looks for user kits. */
juce::File kitFileFor (const juce::File& drive, const juce::String& kitName);

struct Report
{
    juce::StringArray copied, unchanged, failed;
    juce::String toString() const;
};

/** Copies each file into `folder` (made if needed). Each is written under a hidden
    ".<name>.part", which the Mimic ignores, checked, then renamed into place, replacing an
    older file of the same name. A file already there with the same bytes is left alone.
    `names` gives the name each file gets there (default: its own). */
Report send (const juce::Array<juce::File>& files, const juce::File& folder, const juce::StringArray& names = {});
} // namespace emk::MimicDrive
