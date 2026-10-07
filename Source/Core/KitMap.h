#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace emk
{
/** One playable zone of a kit piece, e.g. "Ride" / "Bell" on MIDI note 53. */
struct KitZone
{
    juce::String piece;      // Kick, Snare, Tom 1, Hi-Hat, Ride, Crash 1...
    juce::String label;      // folder/file zone label wav2mci reads: "RIDE1 BELL", "HATS BOW OPEN2"
    int note = 0;            // MIDI note that triggers it in the plugin
    int cc4 = -1;            // hi-hat openness sent before the note (127 closed, 0 open), -1 = none
    bool enabled = true;
};

/** The note-to-zone map for one loaded kit. */
class KitMap
{
public:
    /** General MIDI drum map, used when a plugin exposes no note names. */
    static KitMap generalMidi();

    /** The Roland TD-style e-drum map, with hi-hat openness on CC4. Superior Drummer 3,
        BFD3 and Addictive Drums all ship a preset for it. */
    static KitMap rolandEDrum();

    /** Plain-text maps, one zone per line: piece,zone,note,cc4,record. Lets a map made for one
        library be saved, edited in a spreadsheet and shared. */
    juce::String toCsv() const;
    static KitMap fromCsv (const juce::String&, juce::String& error);

    juce::ValueTree toValueTree() const;
    static KitMap fromValueTree (const juce::ValueTree&);

    juce::Array<KitZone> zones;
    juce::String kitName;    // read from the plugin when it exposes one
};
} // namespace emk
