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

/** Slot names in slot-number order, read from a kit. */
juce::StringArray slotsOf (const mci::MciFile& kit);

struct Report
{
    std::vector<std::pair<juce::String, juce::String>> placed;   // slot, instrument name
    juce::StringArray leftOut;                                    // instruments with no free slot
    juce::StringArray notes;                                      // things to check on the Mimic
};

/** Puts each instrument in a slot (kick, snare, hi-hat, ride, toms smallest first, crashes,
    then Aux and Xtra), and returns slot name -> index into `instruments`. */
std::map<juce::String, size_t> assignSlots (const juce::StringArray& slots, const std::vector<Instrument>& instruments,
                                            const mci::MciFile& kit, Report& report);

/** The template kit with its slots refilled: every slot not given an instrument is emptied.
    Mixer channels keep the template's settings, panned to the centre. Throws mci::Error. */
mci::MciFile build (const mci::MciFile& templateKit, const std::vector<Instrument>& instruments, Report& report);

/** "<kit name>.mdp/[U] <kit name>.kit" in folder, as the Mimic's own kits are stored. */
juce::File kitFile (const juce::File& folder, const juce::String& kitName);

/** Reads every .mci in mciFiles, builds the kit and writes it. Throws mci::Error. */
Report buildFile (const juce::Array<juce::File>& mciFiles, const juce::File& templateKit, const juce::File& out);
} // namespace emk::MimicKit
