#pragma once
#include "Mci.h"
#include <functional>
#include <memory>

namespace emk
{
/** Turns folders of rendered drum hits (WAV) into Pearl Mimic instruments, as
    tools/mci/wav2mci.py does (the reference; tests/test_cpp_converter.py checks both give
    the same instruments).

    Reads the layout SDSE, similar renderers and BatchRecorder write: one folder per zone,
    files named "<piece> - <ZONE> - <hit>v<velocity>.wav", in a folder or a .zip. Folders
    that share a piece name become one instrument, and several folders given together (a
    ride's BOW, BELL and EDGE folders, or several kit pieces) are read as one input. Nothing
    here touches the message thread, so it can run on a background thread. */
class WavToMci
{
public:
    struct Options
    {
        bool choke = true;        // make a Choke Ring from the hardest edge hit when none was recorded
        bool hatAsRide = false;   // also write each hi-hat as a 3-zone ride-type instrument
        int cellsPercent = 100;   // share of each instrument's wave-cell limit used (lighter instruments)
        bool fullLength = false;  // keep every tail as recorded instead of cutting it (maxSeconds)
        juce::String name;        // instrument name, used only when the input holds one piece
    };

    struct Report
    {
        juce::StringArray written;   // full paths of the .mci files
        juce::StringArray skipped;   // "<file>: why"
        juce::StringArray failed;    // "<piece>: why"
        bool stopped = false;
        bool foundHits = false;
        juce::String toString() const;
    };

    /** Converts every piece in input (a folder or a .zip) into outDir. log gets a line per
        step; keepGoing gets the progress (0 to 1) and returns false to stop. Returns a short
        report. */
    static juce::String convert (const juce::File& input, const juce::File& outDir, const Options&,
                                 std::function<void (const juce::String&)> log,
                                 std::function<bool (double progress)> keepGoing);

    static Report run (const juce::File& input, const juce::File& outDir, const Options&,
                       std::function<void (const juce::String&)> log,
                       std::function<bool (double progress)> keepGoing);

    /** The same for several inputs read as one: every WAV under each folder, named relative
        to the folder holding them all. One input behaves exactly as above; among several, a
        .zip (or anything else that is not a folder) is skipped. With no outDir, the files
        go to the folder holding all the inputs. */
    static juce::String convert (const juce::Array<juce::File>& inputs, const juce::File& outDir, const Options&,
                                 std::function<void (const juce::String&)> log,
                                 std::function<bool (double progress)> keepGoing);

    static Report run (const juce::Array<juce::File>& inputs, const juce::File& outDir, const Options&,
                       std::function<void (const juce::String&)> log,
                       std::function<bool (double progress)> keepGoing);

    //==============================================================================
    // The steps, public for tests.

    static constexpr int sampleRate = 44100;   // the Mimic's rate, judging by Pearl's factory content
    static constexpr int maxLayers = 20;       // what the Editor's and Pearl's cymbal files use
    static constexpr int maxRoundRobins = 6;   // per velocity layer, for kicks, snares, toms and cymbals
    static constexpr int hatLayers = 8, hatHits = 32;   // per hi-hat articulation, or it runs to gigabytes

    static constexpr double shortFadeSeconds = 0.5;   // fade-out at the end of a hit that had to be cut

    /** Fade-out for a cut hit: cymbals and rides over the second half of maxSeconds, since they
        are cut while still ringing and a 2 s fade still sounded like a cutoff (Alexandre
        2026-10-14); everything else shortFadeSeconds. */
    static double fadeSeconds (const juce::String& family, bool hasBell);

    /** Longest hit kept for a piece, close to Pearl's factory hits: kick 2.5 s; snare, hi-hat,
        percussion 3 s; tom 5 s; cymbal 16 s, ride-type (with a bell) 20 s. */
    static double maxSeconds (const juce::String& family, bool hasBell);
    /** x cut to maxFrames with its last fadeFrames faded out to 0 along the square of a straight
        line (falls fast, then trails off like a ringing tail); unchanged when shorter. */
    static void fitLength (mci::Samples& x, int maxFrames, int fadeFrames = (int) (shortFadeSeconds * sampleRate + 0.5));

    /** Wave cells the Mimic allows per instrument, shared by its zones; 0 = no limit known. */
    static int cellLimit (const juce::String& instrumentType);

    /** The deepest folder holding every file, or File() when there is none. */
    static juce::File commonFolder (const juce::Array<juce::File>&);

    /** A folder or a zip, read the same way; or several folders read as one, their files
        named relative to the folder holding them all. Among several inputs, anything that
        is not a folder (a .zip included) is left out and listed in skipped(). */
    class Input
    {
    public:
        explicit Input (const juce::File&);
        explicit Input (const juce::Array<juce::File>&);
        ~Input();
        bool isValid() const { return valid; }
        const juce::StringArray& names() const { return entries; }   // in the order Python lists them
        const juce::StringArray& skipped() const { return skippedInputs; }   // "<input>: why"
        juce::MemoryBlock read (const juce::String& name) const;
        bool has (const juce::String& name) const;
        juce::String topFolder() const;

    private:
        juce::File root;   // names are relative to it
        std::unique_ptr<juce::ZipFile> zip;
        juce::StringArray entries, skippedInputs;
        bool valid = false;
    };

    struct Hit
    {
        int velocity = 0;
        juce::String path;
        juce::String blend;   // a second hit mixed 50/50 with this one (made-up side rimshots)
    };
    using Hits = std::vector<Hit>;

    struct Piece
    {
        juce::String name, family;                        // family: cymbal, kick, snare, tom, hat
        std::vector<std::pair<juce::String, Hits>> zones; // in the order they were found
        const Hits* find (const juce::String& zone) const;
        Hits& add (const juce::String& zone);
    };

    /** ("cymbal", "bell") from labels like "RIDE1 BELL", ("hat", "bow:open3") from
        "HATS BOW OPEN3". Throws mci::Error for a label it cannot read. */
    static std::pair<juce::String, juce::String> classify (const juce::String& zoneLabel);
    /** "bow:open3", "pedal", "footsplash", or empty when the label is not a hi-hat's. */
    static juce::String classifyHat (const juce::String& zoneLabel);
    static double opennessRank (const juce::String& level);
    static juce::StringArray pickOpenness (juce::StringArray levels, int steps = 5);

    /** Adds the input's own skipped() entries to skipped first. */
    static std::vector<Piece> scan (const Input&, juce::StringArray& skipped);

    using Shares = std::vector<std::pair<juce::String, int>>;
    /** Splits a cell budget across zones as evenly as possible; a zone with fewer hits than
        its share keeps them all and hands the rest to the others. */
    static Shares shareCells (int budget, const Shares& available);
    static std::vector<Hits> splitLayers (Hits, int maxLayers = WavToMci::maxLayers);
    /** Round robins wanted per velocity layer: 6 on snare heads and rims, 5 on toms', 4
        elsewhere (kick, sidestick, rimclick, cymbals). */
    static int roundRobins (const juce::String& family, const juce::String& zone);
    /** count of the hits, spread evenly over their velocities (the hardest alone for 1). */
    static Hits evenSpread (Hits, int count);
    /** About roundRobins hits per velocity layer: round (n / roundRobins) layers, 1 to
        maxLayers. Hits beyond maxRoundRobins per layer are thinned out evenly across velocities. */
    static std::vector<Hits> roundRobinLayers (Hits, int roundRobins);
    static std::vector<int> minVelocities (const std::vector<Hits>& layers);

    static juce::MemoryBlock thumbnail (int width = 76, int height = 63);

    /** What the Mimic shows for a piece, beyond its sound. */
    struct PieceInfo
    {
        juce::String name, library;   // "Set Displayed Instrument Name / Library Name" in the Editor
        juce::MemoryBlock image;      // PNG, 94x63
    };
    static constexpr const char* infoFile = "mimic.txt";    // "name: ..." and "library: ..." lines
    static constexpr const char* imageFile = "mimic.png";
    /** The piece's mimic.txt and mimic.png, from the folder holding its zone folders (where
        BatchRecorder writes them) or from a zone folder. Empty when there are none. */
    static PieceInfo pieceInfo (const Piece&, const Input&);
    static juce::String infoText (const PieceInfo&);
    static juce::String pieceTitle (const Piece&, const Input&);

    /** Mono samples on the 24-bit scale. Stereo is averaged; 16-bit lands on the Editor's grid.
        Throws mci::Error for anything but 44.1 kHz PCM. */
    static mci::Samples readWav (const juce::MemoryBlock&);

    static mci::MciFile build (const Piece&, const Input&, const juce::String& name, bool choke, bool hatAsRide,
                               const std::function<void (const juce::String&)>& log,
                               const std::function<void()>& onWavRead = {}, bool fullLength = false,
                               int cellsPercent = 100);
};
} // namespace emk
