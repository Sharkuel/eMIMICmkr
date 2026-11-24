#pragma once
#include "Mci.h"

/** Builds Pearl Mimic kit presets (.kit) from instruments eMIMICmkr made.

    A .kit is a .mci container holding only metadata: 26 fixed slots (Kick, Kick Layer, Snare,
    Snare Layer, Hi-Hat, Ride 1, Ride 2, Tom 1-4, Crash 1-2, Aux 1-5, Xtra 1-8), each naming the
    instrument it loads (insstCS<slot>lsin/lsln = the instrument's INST0insnam/INST0libnam) and
    a mixer channel (CME<slot>...), plus trigger pad settings (TPAD...) and buses. A new kit is
    made from one of the user's own kits, so their pad settings carry over; see
    docs/mci-format.md. */
namespace emk::MimicKit
{
/** An instrument as the kit needs it: its settings, read without the audio. */
struct Instrument
{
    juce::File file;
    mci::MciFile settings;

    juce::String name() const { return settings.getString ("INST0insnam"); }
    juce::String type() const { return settings.getString ("INST0instyp"); }
};

Instrument readInstrument (const juce::File&);

/** What a kit asks of the Mimic's memory: the RAM heads of its instruments, which the Mimic
    preloads (each sample's first 32768 values, about 107 KB per wave cell), and their wave cells.
    Pearl's 105 factory kits (Mimic_Factory.lib, 2026-11-24) preload 88 to 169 MB (median 131),
    in 806 to 1633 cells; how far past that a Mimic still plays clean is not known. */
struct Load
{
    juce::int64 bytes = 0;
    int cells = 0, instruments = 0;
    juce::StringArray unknown;   // loaded slots whose instrument file wasn't found: "Ride 1 (22 K Ride)"
    enum class Level { fine, big, tooBig };
    Level level() const;         // fine up to Pearl's biggest kit, big up to 25% past it, then tooBig
    juce::String describe() const;   // "142 MB preloaded in 1,350 wave cells (Pearl's own kits: 88 to 169 MB)"
};
constexpr double factoryLeastMB = 88.4, factoryMedianMB = 131.0, factoryMostMB = 169.1;
Load loadOf (const mci::MciFile& instrumentSettings);
/** The loaded slots of a kit, each found by its instrument's name and library among `known`. */
Load kitLoad (const mci::MciFile& kit, const std::vector<Instrument>& known);
/** .mci files that may hold a kit's instruments: those beside it (and beside its .mdp folder),
    one folder down, read without their audio. Unreadable files are skipped. */
std::vector<Instrument> instrumentsNear (const juce::File& kitFile);

/** Slot names in slot-number order, read from a kit. */
juce::StringArray slotsOf (const mci::MciFile& kit);

struct Report
{
    std::vector<std::pair<juce::String, juce::String>> placed;   // slot, instrument name
    juce::StringArray leftOut;                                    // instruments with no free slot
    juce::StringArray notes;                                      // things to check on the Mimic
    Load load;                                                    // of the instruments placed
};

/** Puts each instrument in a slot (kick, snare, hi-hat, ride, toms smallest first, crashes,
    then Aux and Xtra), and returns slot name -> index into `instruments`. */
std::map<juce::String, size_t> assignSlots (const juce::StringArray& slots, const std::vector<Instrument>& instruments,
                                            const mci::MciFile& kit, Report& report);

/** The template kit with its slots refilled: every slot not given an instrument is emptied.
    Mixer channels keep the template's settings, panned to the centre. Throws mci::Error. */
mci::MciFile build (const mci::MciFile& templateKit, const std::vector<Instrument>& instruments, Report& report);

/** One slot as the kit editor shows it. */
struct Slot
{
    juce::String name, instrument, library, type;   // instrument fields are empty when nothing is loaded
    bool loaded = false, enabled = true;
};
std::vector<Slot> slotList (const mci::MciFile& kit);

/** Puts an instrument in a slot. A slot that held something keeps its mixer channel (EQ,
    compressors, pans); an empty one gets a copy of the first loaded slot's, EQ and compressors
    off. Throws mci::Error. */
void place (mci::MciFile& kit, const juce::String& slot, const Instrument&);
void empty (mci::MciFile& kit, const juce::String& slot);
/** False, with the reason, for a hi-hat outside the Hi-Hat slot or anything else in it. */
bool canTake (const juce::String& slot, const Instrument&, juce::String& why);
void setEnabled (mci::MciFile& kit, const juce::String& slot, bool on);

/** A mic of a slot's mixer channel (CME<slot>mi<n>...). */
struct Mic
{
    juce::String name;    // micn: "Kick In", "OH", "Room"...
    float level = 1.0f;   // micv
    float pan = 0.5f;     // micp, 0 left, 0.5 centre, 1 right
    bool muted = false;   // micm
};
std::vector<Mic> mics (const mci::MciFile& kit, const juce::String& slot);
void setMic (mci::MciFile& kit, const juce::String& slot, int index, const Mic&);

/** A float of a loaded slot's channel, "CME<slot><what>": "iv" (volume, 1 = as recorded) or
    "tn" (tune, 0.5 = as recorded). Setting one on an empty slot does nothing. */
float channelValue (const mci::MciFile& kit, const juce::String& slot, const char* what, float fallback);
void setChannelValue (mci::MciFile& kit, const juce::String& slot, const char* what, float value);

/** Pans every mic of every slot to the centre; returns how many changed. */
int centreAll (mci::MciFile& kit);

/** The template with every slot emptied: a new kit that keeps its pad settings. Throws mci::Error. */
mci::MciFile emptied (const mci::MciFile& templateKit);

/** "[U] My kit.kit" -> "My kit", and the folder that holds the kit (above its .mdp folder). */
juce::String kitName (const juce::File&);
juce::File kitFolderOf (const juce::File&);

/** "<kit name>.mdp/[U] <kit name>.kit" in folder, as the Mimic's own kits are stored. */
juce::File kitFile (const juce::File& folder, const juce::String& kitName);

/** Reads every .mci in mciFiles, builds the kit and writes it. Throws mci::Error. */
Report buildFile (const juce::Array<juce::File>& mciFiles, const juce::File& templateKit, const juce::File& out);
} // namespace emk::MimicKit
