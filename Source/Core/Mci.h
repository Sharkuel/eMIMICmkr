#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/** Reader, writer and builder for Pearl Mimic instrument files (.mci), ported from
    tools/mci/mci.py (the reference implementation) and docs/mci-format.md.

    Samples are ints on a 24-bit scale (full scale = 2^23). Files written by the Editor
    only hold 16-bit mono audio, so their low 8 bits are always zero; quantize() puts
    float audio on that grid. Everything here must give the same bytes as mci.py:
    tests/test_cpp_converter.py compares the two. */
namespace emk::mci
{
using Samples = std::vector<int>;    // one mono sample, 24-bit scale
using Layer = std::vector<Samples>;  // the round robins of a velocity layer
using Layers = std::vector<Layer>;   // velocity layers, soft to hard

constexpr int containerVersion = 3;
constexpr int blockSize = 1024;      // every compressed block is exactly this many bytes
constexpr int blockHeader = 8;       // u8 bits, 24-bit first value (u8 low, i16 high), u32 frames
constexpr int payloadBits = (blockSize - blockHeader) * 8;
constexpr int headPad = 63;          // zero frames in front of every RAM head
constexpr int headMaxFrames = 32768;
constexpr int headMaxBlocks = 64;
constexpr int minBlockBits = 4;     // Pearl never packs deltas narrower than this (see encodeBlocks)
constexpr int fullScale = (1 << 23) - 1;   // largest value a block header (and the Mimic) can hold
constexpr int maxRoundRobins = 16;   // length of every veloLay#smpidx array
constexpr int rtArrayLen = 8;        // length of every release-time array

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Metadata sections, in file order. The sixth has never been seen non-empty. */
enum class Section { i32 = 0, i32Array, f32, f32Array, u8, unknown, string };

/** One metadata value. Strings hold latin-1 bytes, as stored. */
struct Value
{
    Section section = Section::i32;
    int i = 0;                 // i32 and u8
    float f = 0.0f;            // f32
    std::vector<int> ints;     // i32Array
    std::vector<float> floats; // f32Array
    std::string text;          // string

    static Value ofInt (int v)                       { Value x; x.i = v; return x; }
    static Value ofU8 (int v)                        { Value x; x.section = Section::u8; x.i = v; return x; }
    static Value ofFloat (double v)                  { Value x; x.section = Section::f32; x.f = (float) v; return x; }
    static Value ofInts (std::vector<int> v)         { Value x; x.section = Section::i32Array; x.ints = std::move (v); return x; }
    static Value ofFloats (std::vector<float> v)     { Value x; x.section = Section::f32Array; x.floats = std::move (v); return x; }
    static Value ofString (const juce::String& v);
};

/** A .mci file split into its parts. */
struct MciFile
{
    std::map<std::string, Value> meta;   // sorted by key bytes, as the file stores them
    juce::MemoryBlock image;             // PNG thumbnail shown by the Editor
    juce::MemoryBlock heads;             // float32 RAM preload of every sample
    juce::MemoryBlock data;              // compressed blocks of every sample
    int version = containerVersion;

    const Value* find (const std::string& key) const;
    int getInt (const std::string& key, int fallback = 0) const;
    float getFloat (const std::string& key, float fallback = 0.0f) const;
    std::vector<int> getInts (const std::string& key) const;
    std::vector<float> getFloats (const std::string& key) const;
    juce::String getString (const std::string& key) const;
    void set (const std::string& key, Value v) { meta[key] = std::move (v); }
};

/** Throws mci::Error when the file is not a valid container. */
MciFile readMci (const void* bytes, size_t size);
MciFile readMci (const juce::File&);

juce::MemoryBlock writeMci (const MciFile&);
/** Writes through a temporary file, so a failed write leaves no half-written instrument. */
juce::Result writeMciFile (const MciFile&, const juce::File&);

/** Reads only the header and metadata: a kit preset (.kit, which has nothing else), or the
    settings of a .mci without loading its audio. Throws mci::Error. */
MciFile readSettings (const void* bytes, size_t size);
MciFile readSettings (const juce::File&);

/** A kit preset (.kit): the header and metadata alone, no audio and no checksum trailer. */
juce::MemoryBlock writeKit (const MciFile&);
juce::Result writeKitFile (const MciFile&, const juce::File&);

/** Decodes pool sample `index` (all channels interleaved). */
Samples sample (const MciFile&, int index);

//==============================================================================
// Audio codec

/** Appends the compressed blocks of x to out and returns the frame count of every block.
    A block of n frames stores n deltas; the last one bridges to the next block's first frame. */
std::vector<int> encodeBlocks (const Samples& x, juce::MemoryOutputStream& out);
Samples decodeBlocks (const void* blocks, size_t size);

/** Blocks preloaded into RAM (nobu) and the head length in frames (desc). */
struct HeadLayout
{
    int nobu = 0, desc = 0;
};
HeadLayout headLayout (const std::vector<int>& blockFrames);
void encodeHead (const Samples&, int desc, juce::MemoryOutputStream& out);

/** Loudness envelope, one value per 1024 frames. The Editor's exact formula is unknown;
    see mci.py for why this one-pole follower was chosen. */
std::vector<int> rmsEnvelope (const Samples&);

//==============================================================================
// Building instruments

struct Articulation
{
    juce::String name;                       // "Bow", "Snare Rimshot"...
    std::optional<int> zoneType;             // see zoneTypes(); none = only reached through a controller zone
    Layers layers;
    std::vector<int> minVelocities;          // empty = computed from loudness
    std::pair<double, double> releaseBottom { 0.3, 0.5 };   // (minrtb, minrtt)
    std::pair<double, double> releaseTop { 1.5, 3.0 };      // (maxrtb, maxrtt)
    bool choke = false;
    std::optional<int> artid;                // none = the articulation's index
    int noteon = 0;                          // MIDI note; factory hi-hats set it, the Editor writes 0
    std::optional<bool> mute;                // none = same as choke
};

/** A hi-hat zone whose articulation the pedal picks: zone type, artids closed to open. */
struct ControllerZone
{
    int zoneType = 0;
    std::vector<int> artids;
};

/** Zone type ids seen in Editor files, by the names mci.py uses ("ride bow", "drum center"...). */
const std::map<juce::String, int>& zoneTypes();
int zoneType (const juce::String& name);

/** (micpos, mict) the Editor writes for an instrument type; anything else gets (0, 0). */
std::pair<int, int> micDefaults (const juce::String& instrumentType);

/** Bow/Bell/Edge/Choke Ring in the order the Editor saves a ride (also a 3-zone crash).
    An empty choke leaves the Choke Ring silent, as the Editor does. */
std::vector<Articulation> rideArticulations (Layers bow, Layers bell, Layers edge, Layers choke = {},
                                             std::pair<double, double> releaseBottom = { 0.5, 0.5 },
                                             std::pair<double, double> releaseTop = { 3.0, 5.0 });

/** Edge/Bow/Choke Ring for a 2-zone crash or splash (type "Cymbal"). */
std::vector<Articulation> cymbalArticulations (Layers edge, Layers bow, Layers choke = {},
                                               std::pair<double, double> releaseBottom = { 0.5, 0.5 },
                                               std::pair<double, double> releaseTop = { 3.0, 5.0 });

/** Hi-hat zone ids, from Pearl's factory hi-hats. */
enum HatZone { hatEdge = 80, hatTip = 81, hatShank = 82, hatPedal = 83, hatFootsplash = 84 };
constexpr int hatOpennessSteps = 5;
const juce::StringArray& hatOpenness();   // "Closed", "1/4 Open"... "Open"

struct HatLayout
{
    std::vector<Articulation> articulations;
    std::vector<ControllerZone> controllerZones;
};

/** A hi-hat in the factory layout (type "Hat"). tip, shank and edge hold five zones each,
    closed to open. Throws when a group does not have five. */
HatLayout hatArticulations (std::vector<Layers> tip, std::vector<Layers> shank, std::vector<Layers> edge,
                            Layers pedal, Layers footsplash, Layers transition = {});

/** Turns an open cymbal hit into a choke-ring sample: the ring without the hit. The attack is
    cut where the envelope, after its peak, first falls 6 dB below it (at least 20 ms after
    the peak), then up to `length` seconds of ring are kept, faded in and out. */
std::vector<double> makeChoke (const std::vector<double>& samples, int rate = 44100, double length = 3.0,
                               double fadeIn = 0.005, double fadeOut = 1.0);

/** Floats in [-1, 1] to the 16-bit grid on the 24-bit scale, as the Editor stores them. */
Samples quantize (const float* samples, size_t count);
Samples quantize (const std::vector<double>& samples);

/** Per-layer loudness (soft to hard) to minimum MIDI velocities: 127 * sqrt(level / loudest),
    strictly rising, within 1..127. */
std::vector<int> velocityThresholds (const std::vector<double>& loudness);

MciFile buildInstrument (const juce::String& name, const juce::String& instrumentType,
                         const std::vector<Articulation>& articulations, const juce::MemoryBlock& image,
                         const juce::String& library = {}, const juce::String& mic = {},
                         double sampleVolume = 0.52, const std::vector<ControllerZone>& controllerZones = {});
} // namespace emk::mci
