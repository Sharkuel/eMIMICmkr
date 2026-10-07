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
    that share a piece name become one instrument. Nothing here touches the message
    thread, so it can run on a background thread. */
class WavToMci
{
public:
    struct Options
    {
        bool choke = true;        // make a Choke Ring from the hardest edge hit when none was recorded
        bool hatAsRide = false;   // also write each hi-hat as a 3-zone ride-type instrument
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

    //==============================================================================
    // The steps, public for tests.

    static constexpr int sampleRate = 44100;   // the Mimic's rate, judging by Pearl's factory content
    static constexpr int maxLayers = 20;       // what the Editor's and Pearl's cymbal files use
    static constexpr int hatLayers = 8, hatHits = 32;   // per hi-hat articulation, or it runs to gigabytes

    /** Wave cells the Mimic allows per instrument, shared by its zones; 0 = no limit known. */
    static int cellLimit (const juce::String& instrumentType);

    /** A folder or a zip, read the same way. */
    class Input
    {
    public:
        explicit Input (const juce::File&);
        ~Input();
        bool isValid() const { return valid; }
        const juce::StringArray& names() const { return entries; }   // in the order Python lists them
        juce::MemoryBlock read (const juce::String& name) const;
        juce::String topFolder() const;

    private:
        juce::File root;
        std::unique_ptr<juce::ZipFile> zip;
        juce::StringArray entries;
        bool valid = false;
    };

    struct Hit
    {
        int velocity = 0;
        juce::String path;
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

    static std::vector<Piece> scan (const Input&, juce::StringArray& skipped);

    using Shares = std::vector<std::pair<juce::String, int>>;
    /** Splits a cell budget across zones as evenly as possible; a zone with fewer hits than
        its share keeps them all and hands the rest to the others. */
    static Shares shareCells (int budget, const Shares& available);
    static std::vector<Hits> splitLayers (Hits, int maxLayers = WavToMci::maxLayers);
    static std::vector<int> minVelocities (const std::vector<Hits>& layers);

    static juce::MemoryBlock thumbnail (int width = 76, int height = 63);
    static juce::String pieceTitle (const Piece&, const Input&);

    /** Mono samples on the 24-bit scale. Stereo is averaged; 16-bit lands on the Editor's grid.
        Throws mci::Error for anything but 44.1 kHz PCM. */
    static mci::Samples readWav (const juce::MemoryBlock&);

    static mci::MciFile build (const Piece&, const Input&, const juce::String& name, bool choke, bool hatAsRide,
                               const std::function<void (const juce::String&)>& log,
                               const std::function<void()>& onWavRead = {});
};
} // namespace emk
