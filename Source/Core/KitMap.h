#pragma once
#include <juce_core/juce_core.h>

namespace emk
{
/** One playable zone of a kit piece, e.g. "Ride" / "Bell" on MIDI note 53. */
struct KitZone
{
    juce::String piece;      // Kick, Snare, Tom 1, Hi-Hat, Ride, Crash 1...
    juce::String zone;       // Center, Rimshot, Bow, Bell, Edge...
    int note = 0;            // MIDI note that triggers it in the plugin
    int mimicZoneType = 0;   // INST0zoneType id in the .mci (0 = not known yet)
    bool enabled = true;
};

/** The note-to-zone map for one loaded kit. */
class KitMap
{
public:
    /** General MIDI drum map, used when a plugin exposes no note names. */
    static KitMap generalMidi();

    juce::Array<KitZone> zones;
    juce::String kitName;    // read from the plugin when it exposes one
};
} // namespace emk
