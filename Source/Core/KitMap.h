#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <map>

namespace emk
{
/** One playable zone of a kit piece, e.g. "Ride" / "Bell" on MIDI note 53. */
struct KitZone
{
    KitZone() = default;
    KitZone (juce::String p, juce::String l, int n, int cc = -1, bool on = true, juce::String nameInPlugin = {})
        : piece (std::move (p)), label (std::move (l)), note (n), cc4 (cc), enabled (on), pluginName (std::move (nameInPlugin)) {}

    juce::String piece;      // Kick, Snare, Tom 1, Hi-Hat, Ride, Crash 1...
    juce::String label;      // folder/file zone label wav2mci reads: "RIDE1 BELL", "HATS BOW OPEN2"
    int note = 0;            // MIDI note that triggers it in the plugin
    int cc4 = -1;            // hi-hat openness sent before the note (127 closed, 0 open), -1 = none
    bool enabled = true;
    juce::String pluginName; // the plugin's own name for this note, when it gives one
    int hits = 0;            // hits the user chose for this zone; 0 = share the piece's budget
    juce::String instrument; // the plugin's own ID for the drum, e.g. "SL-DRUMFACTORYARCADES/SD01_01"

    // What the Mimic shows for the piece; every zone of a piece carries the same.
    juce::String name;       // instrument name, e.g. "5x14 DW Timeless Timber Maple"; empty = from the piece
    juce::String library;    // library name, e.g. "SD3 Drum Factory Arcades"; empty = the kit name
    juce::MemoryBlock image; // 94x63 PNG

    /** The library part of instrument: "SL-DRUMFACTORYARCADES". */
    juce::String libraryId() const { return instrument.upToFirstOccurrenceOf ("/", false, false); }
};

/** What the user told the app about one drum, kept by the plugin's ID for it. */
struct DrumInfo
{
    juce::String name;
    juce::MemoryBlock image;
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

    /** The map a Superior Drummer 3 kit uses, from its preset (.sd3p) or plugin state: each
        instrument slot (Kick, Snare, Racktom1, Ride4...), its articulations and the notes
        they're on. The main zones are ticked; the rest are added unticked. Empty, with error
        set, when the text isn't SD3's. */
    static KitMap fromSd3 (const juce::String& text, juce::String& error);

    /** What you told the app about drums, by their plugin ID, and library names by the plugin's
        library ID: every zone whose drum is known takes its name and image, and its library's
        name. Two pieces never end up sharing one name. */
    void applyNames (const std::map<juce::String, DrumInfo>& drums,
                     const std::map<juce::String, juce::String>& libraries = {});

    /** A Mimic name from a picture's file name: "5x14_DW-TimelessTimberMaple.png" ->
        "5x14 DW Timeless Timber Maple". Empty for names that say nothing, like "Screenshot_2026". */
    static juce::String nameFromFileName (const juce::String& fileName);

    /** Moves zones[index] into piece: it takes that piece's name, library, image and drum, or,
        for a piece no other row has, drops its old ones. Only this row moves. */
    void movePiece (int index, const juce::String& piece);

    /** Whether the rows of piece go together; "?" rows (notes found but not sorted out) don't. */
    static bool sharesPiece (const juce::String& piece);

    /** Sets name, library or image on every zone of zones[index]'s piece ("?" rows: that row only). */
    void setPieceName (int index, const juce::String&);
    void setPieceLibrary (int index, const juce::String&);
    void setPieceImage (int index, const juce::MemoryBlock&);

    /** Finds SD3's kit text inside a plugin state or preset file, which may wrap it. */
    static juce::String sd3TextIn (const juce::MemoryBlock&);

    /** How many hits to record for zones[index] so the piece fits the Mimic's wave-cell limit
        for its instrument type (kick 64, snare 300, tom 200, cymbal 200, ride 300, percussion 200,
        shared by the piece's ticked zones; one cymbal cell goes to the choke ring). Hi-hat
        positions get 32, what the converter keeps. Never more than maxHits.
        A zone the user gave a count keeps it; the piece's other ticked zones share what's left.
        With fitLimits off, every zone gets its own count or maxHits. */
    int hitsFor (int index, int maxHits, bool fitLimits = true) const;

    /** Sets zones[index]'s count (0 = back to an even share), held within what the piece can
        still give it so its other ticked zones keep at least one hit each. Returns the count set. */
    int setHits (int index, int count, bool fitLimits = true);

    /** Guesses the piece and zone label from a plugin's note name, e.g. "Ride 1 Bell" ->
        { "Ride", "RIDE1 BELL" }. Empty when the name isn't recognised. */
    static std::pair<juce::String, juce::String> guessFromName (const juce::String& noteName);

    juce::ValueTree toValueTree() const;
    static KitMap fromValueTree (const juce::ValueTree&);

    juce::Array<KitZone> zones;
    juce::String kitName;    // read from the plugin when it exposes one
};
} // namespace emk
