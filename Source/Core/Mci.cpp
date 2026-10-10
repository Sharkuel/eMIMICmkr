#include "Mci.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace emk::mci
{
namespace
{
const char magic[] = "mimic_storage";     // 14 bytes with its null
const char trailerTag[] = "MMKCSM";       // 7 bytes with its null
constexpr size_t magicSize = 14, trailerTagSize = 7, trailerSize = trailerTagSize + 16;
constexpr size_t headerSize = magicSize + 8;

const Section sectionOrder[] = { Section::i32, Section::i32Array, Section::f32, Section::f32Array,
                                 Section::u8, Section::unknown, Section::string };

// Python's round(): halves go to the even neighbour. nearbyint does that in the default
// rounding mode, which nothing here changes.
int roundHalfEven (double x)
{
    return (int) std::nearbyint (x);
}

std::string toLatin1 (const juce::String& s)
{
    std::string out;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        out += (char) (unsigned char) (c < 256 ? c : '?');   // mci.py would refuse such a name
    }
    return out;
}

juce::String fromLatin1 (const std::string& s)
{
    juce::String out;
    out.preallocateBytes (s.size() * 2);
    for (auto c : s)
        out += juce::String::charToString ((juce::juce_wchar) (unsigned char) c);
    return out;
}

std::string stripNulls (const char* p, size_t n)
{
    while (n > 0 && p[n - 1] == 0)
        --n;
    return std::string (p, n);
}

/** Bounds-checked little-endian reads over the decompressed metadata. */
struct Reader
{
    const juce::uint8* p;
    size_t size, off = 0;

    void need (size_t n) const
    {
        if (off + n > size)
            throw Error ("metadata ends early");
    }
    juce::uint32 u32()
    {
        need (4);
        const auto v = juce::ByteOrder::littleEndianInt (p + off);
        off += 4;
        return v;
    }
    int i32() { return (int) u32(); }
    float f32()
    {
        const auto bits = u32();
        float v;
        std::memcpy (&v, &bits, 4);
        return v;
    }
    std::string bytes()
    {
        const auto n = u32();
        need (n);
        auto s = stripNulls (reinterpret_cast<const char*> (p + off), n);
        off += n;
        return s;
    }
};

void writeKeyed (juce::MemoryOutputStream& out, const std::string& s)
{
    out.writeInt ((int) s.size() + 1);
    out.write (s.data(), s.size());
    out.writeByte (0);
}

juce::MemoryBlock writeMeta (const MciFile& f)
{
    juce::MemoryOutputStream out;
    for (auto section : sectionOrder)
    {
        int count = 0;
        for (const auto& [k, v] : f.meta)
            count += v.section == section ? 1 : 0;
        out.writeInt (count);
        for (const auto& [k, v] : f.meta)
        {
            if (v.section != section)
                continue;
            writeKeyed (out, k);
            switch (section)
            {
                case Section::i32:      out.writeInt (v.i); break;
                case Section::i32Array: out.writeInt ((int) v.ints.size()); for (auto x : v.ints) out.writeInt (x); break;
                case Section::f32:      out.writeFloat (v.f); break;
                case Section::f32Array: out.writeInt ((int) v.floats.size()); for (auto x : v.floats) out.writeFloat (x); break;
                case Section::u8:       out.writeByte ((char) v.i); break;
                case Section::string:   writeKeyed (out, v.text); break;
                case Section::unknown:  throw Error ("cannot write a value of the unknown metadata section (key " + k + ")");
            }
        }
    }
    out.writeInt ((int) f.image.getSize());
    out << f.image;
    return out.getMemoryBlock();
}

void readMeta (const juce::MemoryBlock& raw, MciFile& f)
{
    Reader r { static_cast<const juce::uint8*> (raw.getData()), raw.getSize() };
    for (auto section : sectionOrder)
    {
        for (auto n = r.u32(); n > 0; --n)
        {
            const auto key = r.bytes();
            Value v;
            v.section = section;
            switch (section)
            {
                case Section::i32:      v.i = r.i32(); break;
                case Section::i32Array: for (auto c = r.u32(); c > 0; --c) v.ints.push_back (r.i32()); break;
                case Section::f32:      v.f = r.f32(); break;
                case Section::f32Array: for (auto c = r.u32(); c > 0; --c) v.floats.push_back (r.f32()); break;
                case Section::u8:       r.need (1); v.i = r.p[r.off++]; break;
                case Section::string:   v.text = r.bytes(); break;
                case Section::unknown:  throw Error ("value type of metadata section 6 is unknown (key " + key + ")");
            }
            f.meta[key] = std::move (v);
        }
    }
    const auto imageSize = r.u32();
    r.need (imageSize);
    f.image.replaceAll (r.p + r.off, imageSize);
    r.off += imageSize;
    if (r.off != r.size)
        throw Error (std::to_string (r.size - r.off) + " unexpected bytes after the metadata");
}

juce::MemoryBlock zlibCompress (const juce::MemoryBlock& raw, int level)
{
    juce::MemoryOutputStream out;
    {
        juce::GZIPCompressorOutputStream z (out, level);   // windowBits 0 = a zlib stream, as Python writes
        z.write (raw.getData(), raw.getSize());
    }
    return out.getMemoryBlock();
}

juce::uint32 adler32 (const juce::MemoryBlock& b)
{
    juce::uint32 a = 1, s = 0;
    auto* p = static_cast<const juce::uint8*> (b.getData());
    for (size_t i = 0; i < b.getSize(); ++i)
    {
        a = (a + p[i]) % 65521u;
        s = (s + a) % 65521u;
    }
    return (s << 16) | a;
}

juce::MemoryBlock headerAndMeta (const MciFile& f)
{
    const auto raw = writeMeta (f);
    juce::MemoryOutputStream out;
    out.write (magic, magicSize);
    out.writeInt (f.version);
    out.writeInt ((int) raw.getSize());
    out << zlibCompress (raw, 1);   // the Editor uses level 1
    return out.getMemoryBlock();
}

/** Reads `bits` bits at bit position pos of a stream stored MSB first in little-endian u32 words. */
juce::uint32 readBits (const juce::uint8* payload, int pos, int bits)
{
    juce::uint64 v = 0;
    const int first = pos / 32, last = (pos + bits - 1) / 32;
    for (int w = first; w <= last; ++w)
        v = (v << 32) | juce::ByteOrder::littleEndianInt (payload + 4 * w);
    const int end = (last + 1) * 32;
    v >>= end - (pos + bits);
    return (juce::uint32) (v & ((juce::uint64 (1) << bits) - 1));
}

int bitsFor (juce::int64 delta)
{
    // |delta| < 2^(bits-1), as the Editor sizes them
    auto a = (juce::uint64) (delta < 0 ? -delta : delta);
    int length = 0;
    for (; a != 0; a >>= 1)
        ++length;
    return std::max (1, length + 1);
}
} // namespace

//==============================================================================
Value Value::ofString (const juce::String& v)
{
    Value x;
    x.section = Section::string;
    x.text = toLatin1 (v);
    return x;
}

const Value* MciFile::find (const std::string& key) const
{
    const auto it = meta.find (key);
    return it == meta.end() ? nullptr : &it->second;
}

int MciFile::getInt (const std::string& key, int fallback) const
{
    const auto* v = find (key);
    return v == nullptr ? fallback : v->i;
}

float MciFile::getFloat (const std::string& key, float fallback) const
{
    const auto* v = find (key);
    return v == nullptr ? fallback : v->f;
}

std::vector<int> MciFile::getInts (const std::string& key) const
{
    const auto* v = find (key);
    return v == nullptr ? std::vector<int>() : v->ints;
}

std::vector<float> MciFile::getFloats (const std::string& key) const
{
    const auto* v = find (key);
    return v == nullptr ? std::vector<float>() : v->floats;
}

juce::String MciFile::getString (const std::string& key) const
{
    const auto* v = find (key);
    return v == nullptr ? juce::String() : fromLatin1 (v->text);
}

//==============================================================================
MciFile readMci (const void* bytes, size_t size)
{
    auto* d = static_cast<const juce::uint8*> (bytes);
    if (size < headerSize + trailerSize || std::memcmp (d, magic, magicSize) != 0)
        throw Error ("not a Mimic file (bad magic)");
    MciFile f;
    f.version = (int) juce::ByteOrder::littleEndianInt (d + 14);
    const auto metaSize = juce::ByteOrder::littleEndianInt (d + 18);
    if (f.version != containerVersion)
        throw Error ("unsupported container version " + std::to_string (f.version));
    const auto end = size - trailerSize;
    if (std::memcmp (d + end, trailerTag, trailerTagSize) != 0)
        throw Error ("missing MMKCSM trailer");
    if (juce::MD5 (d, end).getRawChecksumData() != juce::MemoryBlock (d + end + trailerTagSize, 16))
        throw Error ("MD5 checksum mismatch");

    // JUCE's zlib stream does not say where the compressed metadata ends, so it is found
    // from the far side: the audio runs up to the trailer, and the largest dofs + dlen is
    // its length. The zlib stream's own Adler-32 confirms the guess.
    juce::MemoryBlock raw;
    {
        juce::MemoryInputStream in (d + headerSize, end - headerSize, false);
        juce::GZIPDecompressorInputStream z (in);
        raw.setSize (metaSize);
        if ((size_t) z.read (raw.getData(), (int) raw.getSize()) != raw.getSize())
            throw Error ("metadata length mismatch");
    }
    readMeta (raw, f);

    size_t headBytes = 0, audioEnd = 0;
    for (auto n : f.getInts ("INST0pool0desc"))
        headBytes += (size_t) n * 4;
    audioEnd = headBytes;
    const auto dofs = f.getInts ("INST0pool0dofs"), dlen = f.getInts ("INST0pool0dlen");
    for (size_t i = 0; i < std::min (dofs.size(), dlen.size()); ++i)
        audioEnd = std::max (audioEnd, (size_t) dofs[i] + (size_t) dlen[i]);
    if (audioEnd + headerSize + 6 > end)
        throw Error ("the audio does not fit in the file");
    const auto base = end - audioEnd;
    if (juce::ByteOrder::bigEndianInt (d + base - 4) != adler32 (raw))
        throw Error ("cannot find the end of the metadata stream");

    f.heads.replaceAll (d + base, headBytes);
    f.data.replaceAll (d + base + headBytes, end - base - headBytes);
    return f;
}

MciFile readMci (const juce::File& file)
{
    juce::MemoryBlock b;
    if (! file.loadFileAsData (b))
        throw Error ("cannot read " + file.getFullPathName().toStdString());
    return readMci (b.getData(), b.getSize());
}

juce::MemoryBlock writeMci (const MciFile& f)
{
    auto body = headerAndMeta (f);
    body.append (f.heads.getData(), f.heads.getSize());
    body.append (f.data.getData(), f.data.getSize());
    const auto md5 = juce::MD5 (body).getRawChecksumData();
    body.append (trailerTag, trailerTagSize);
    body.append (md5.getData(), md5.getSize());
    return body;
}

juce::Result writeMciFile (const MciFile& f, const juce::File& file)
{
    // Streamed rather than built in memory: a ride can run to hundreds of megabytes.
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return juce::Result::fail ("cannot write " + file.getFullPathName());
        out << headerAndMeta (f) << f.heads << f.data;
        out.flush();
        if (out.getStatus().failed())
            return out.getStatus();
    }
    juce::MemoryBlock md5;
    {
        juce::FileInputStream in (temp.getFile());
        md5 = juce::MD5 (in).getRawChecksumData();
    }
    {
        juce::FileOutputStream out (temp.getFile());   // appends
        out.write (trailerTag, trailerTagSize);
        out << md5;
        out.flush();
        if (out.getStatus().failed())
            return out.getStatus();
    }
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot write " + file.getFullPathName());
    return juce::Result::ok();
}

//==============================================================================
namespace
{
MciFile settingsFrom (juce::InputStream& in)
{
    juce::uint8 header[headerSize];
    if (in.read (header, (int) headerSize) != (int) headerSize || std::memcmp (header, magic, magicSize) != 0)
        throw Error ("not a Mimic file (bad magic)");
    MciFile f;
    f.version = (int) juce::ByteOrder::littleEndianInt (header + 14);
    const auto metaSize = juce::ByteOrder::littleEndianInt (header + 18);
    if (f.version != containerVersion)
        throw Error ("unsupported container version " + std::to_string (f.version));
    juce::MemoryBlock raw (metaSize);
    juce::GZIPDecompressorInputStream z (in);
    if ((size_t) z.read (raw.getData(), (int) raw.getSize()) != raw.getSize())
        throw Error ("metadata length mismatch");
    readMeta (raw, f);
    return f;
}
} // namespace

MciFile readSettings (const void* bytes, size_t size)
{
    juce::MemoryInputStream in (bytes, size, false);
    return settingsFrom (in);
}

MciFile readSettings (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        throw Error ("cannot read " + file.getFullPathName().toStdString());
    return settingsFrom (in);
}

juce::MemoryBlock writeKit (const MciFile& f)
{
    return headerAndMeta (f);
}

juce::Result writeKitFile (const MciFile& f, const juce::File& file)
{
    juce::TemporaryFile temp (file);
    const auto bytes = writeKit (f);
    if (! temp.getFile().replaceWithData (bytes.getData(), bytes.getSize()) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Cannot write " + file.getFullPathName());
    return juce::Result::ok();
}

Samples sample (const MciFile& f, int index)
{
    const auto dofs = f.getInts ("INST0pool0dofs"), dlen = f.getInts ("INST0pool0dlen");
    if (index < 0 || (size_t) index >= dofs.size() || (size_t) index >= dlen.size())
        throw Error ("no pool sample " + std::to_string (index));
    const auto ofs = (juce::int64) dofs[(size_t) index] - (juce::int64) f.heads.getSize();
    const auto n = (juce::int64) dlen[(size_t) index];
    if (ofs < 0 || ofs + n > (juce::int64) f.data.getSize())
        throw Error ("pool sample " + std::to_string (index) + " lies outside the data");
    return decodeBlocks (static_cast<const char*> (f.data.getData()) + ofs, (size_t) n);
}

//==============================================================================
std::vector<int> encodeBlocks (const Samples& x, juce::MemoryOutputStream& out)
{
    const int total = (int) x.size();
    std::vector<juce::uint8> need ((size_t) std::max (0, total - 1));
    for (size_t k = 0; k < need.size(); ++k)
        need[k] = (juce::uint8) bitsFor ((juce::int64) x[k + 1] - x[k]);

    std::vector<int> frames;
    std::vector<juce::uint8> widest;   // running maximum of need over the block's deltas
    juce::uint32 words[payloadBits / 32];
    int p = 0;
    while (p < total)
    {
        const int available = total - 1 - p;   // deltas left: the last frame has none
        widest.resize ((size_t) std::min (payloadBits, available));
        for (size_t k = 0; k < widest.size(); ++k)
            widest[k] = std::max (need[(size_t) p + k], k > 0 ? widest[k - 1] : juce::uint8 (1));

        // The smallest width that fits a full block, as mci.py picks it.
        int bits = 1, n = 0;
        for (;; ++bits)
        {
            if (bits > 32)
                throw Error ("delta does not fit in 32 bits");
            n = std::min (payloadBits / bits, total - p);
            const int deltas = std::min (n, available);
            if (deltas == 0 || widest[(size_t) deltas - 1] <= bits)
                break;
        }

        std::fill (std::begin (words), std::end (words), 0u);
        const auto mask = bits == 32 ? 0xffffffffu : (juce::uint32 (1) << bits) - 1;
        int pos = 0;
        for (int k = 0; k < n && p + k + 1 < total; ++k, pos += bits)
        {
            const auto v = (juce::uint32) ((juce::int64) x[(size_t) (p + k + 1)] - x[(size_t) (p + k)]) & mask;
            const int w = pos / 32, space = 32 - pos % 32;
            if (bits <= space)
            {
                words[w] |= v << (space - bits);
            }
            else
            {
                words[w] |= v >> (bits - space);
                words[w + 1] |= v << (32 - (bits - space));
            }
        }

        const auto first = x[(size_t) p];
        out.writeByte ((char) bits);
        out.writeByte ((char) (first & 0xff));
        out.writeShort ((short) (first >> 8));
        out.writeInt (n);
        for (auto word : words)
            out.writeInt ((int) word);
        frames.push_back (n);
        p += n;
    }
    return frames;
}

Samples decodeBlocks (const void* blocks, size_t size)
{
    auto* d = static_cast<const juce::uint8*> (blocks);
    Samples out;
    for (size_t off = 0; off + blockSize <= size; off += blockSize)
    {
        const int bits = d[off];
        const int low = d[off + 1];
        const int high = (juce::int16) juce::ByteOrder::littleEndianShort (d + off + 2);
        const auto n = juce::ByteOrder::littleEndianInt (d + off + 4);
        if (bits < 1 || bits > 32 || (juce::uint64) n * (juce::uint64) bits > (juce::uint64) payloadBits + (juce::uint64) bits)
            throw Error ("bad block header");
        auto x = (juce::int64) high * 256 + low;
        out.push_back ((int) x);
        const auto half = juce::int64 (1) << (bits - 1);
        for (juce::uint32 k = 0; k + 1 < n; ++k)
        {
            auto delta = (juce::int64) readBits (d + off + blockHeader, (int) k * bits, bits);
            if (delta >= half)
                delta -= juce::int64 (1) << bits;
            x += delta;
            out.push_back ((int) x);
        }
    }
    return out;
}

HeadLayout headLayout (const std::vector<int>& blockFrames)
{
    int cum = 0, nobu = 0;
    const int blocks = (int) blockFrames.size();
    while (nobu < blocks && nobu < headMaxBlocks
           && headPad + 1 + cum + blockFrames[(size_t) nobu] <= headMaxFrames)
        cum += blockFrames[(size_t) nobu++];
    if (nobu < blocks)
        return { nobu, headPad + cum + 1 };   // includes the next block's first frame
    return { nobu, headPad + cum };
}

void encodeHead (const Samples& samples, int desc, juce::MemoryOutputStream& out)
{
    for (int i = 0; i < headPad; ++i)
        out.writeFloat (0.0f);
    const auto n = std::min ((size_t) std::max (0, desc - headPad), samples.size());
    for (size_t i = 0; i < n; ++i)
        out.writeFloat ((float) (samples[i] / 8388608.0));
}

std::vector<int> rmsEnvelope (const Samples& samples)
{
    const double a = std::exp (-1.0 / 1024.0);
    std::vector<int> out;
    double e = 0.0;
    const size_t n = (samples.size() / 1024) * 1024;
    for (size_t i = 0; i < n; ++i)
    {
        // Kept as two products and a sum, so it rounds as mci.py does (no fused multiply-add,
        // which CMakeLists.txt turns off for this file).
        e = a * e + (1.0 - a) * std::abs ((double) samples[i]);
        if (i % 1024 == 1023)
            out.push_back ((int) (e * 1.04));
    }
    return out;
}

//==============================================================================
const std::map<juce::String, int>& zoneTypes()
{
    static const std::map<juce::String, int> types {
        { "kick", 1 },
        { "drum center", 1 }, { "drum rimclick", 2 }, { "drum rimshot", 3 },
        { "snare sidestick", 4 }, { "snare rimshot side", 5 }, { "snare side", 6 },
        { "ride bow", 40 }, { "ride choke", 41 }, { "ride bell", 42 }, { "ride edge", 43 },   // also 2-zone cymbals
        { "perc main", 200 }, { "perc secondary", 201 }, { "perc additional 1", 202 }, { "perc additional 2", 203 },
    };
    return types;
}

int zoneType (const juce::String& name)
{
    const auto it = zoneTypes().find (name);
    if (it == zoneTypes().end())
        throw Error ("unknown zone type " + name.toStdString());
    return it->second;
}

std::pair<int, int> micDefaults (const juce::String& instrumentType)
{
    if (instrumentType == "Kick")
        return { 2, 0 };
    if (instrumentType == "Ride" || instrumentType == "Cymbal")
        return { 5, 1 };
    return { 0, 0 };
}

namespace
{
std::vector<Articulation> cymbalZones (std::vector<std::pair<juce::String, Layers>> zones, const juce::StringArray& keys,
                                       Layers choke, std::pair<double, double> bottom, std::pair<double, double> top)
{
    std::vector<Articulation> arts;
    for (size_t i = 0; i < zones.size(); ++i)
    {
        Articulation a;
        a.name = zones[i].first;
        a.zoneType = zoneType (keys[(int) i]);
        a.layers = std::move (zones[i].second);
        a.releaseBottom = bottom;
        a.releaseTop = top;
        arts.push_back (std::move (a));
    }
    Articulation c;
    c.name = "Choke Ring";
    c.zoneType = zoneType ("ride choke");
    c.choke = true;
    if (! choke.empty())
    {
        c.layers = std::move (choke);
        c.releaseBottom = bottom;
        c.releaseTop = top;
    }
    else
    {
        c.releaseBottom = { 1.0, 1.0 };
        c.releaseTop = { 1.0, 1.0 };
    }
    arts.push_back (std::move (c));
    return arts;
}
} // namespace

std::vector<Articulation> rideArticulations (Layers bow, Layers bell, Layers edge, Layers choke,
                                             std::pair<double, double> releaseBottom, std::pair<double, double> releaseTop)
{
    std::vector<std::pair<juce::String, Layers>> zones;
    zones.emplace_back ("Bow", std::move (bow));
    zones.emplace_back ("Bell", std::move (bell));
    zones.emplace_back ("Edge", std::move (edge));
    return cymbalZones (std::move (zones), { "ride bow", "ride bell", "ride edge" }, std::move (choke), releaseBottom, releaseTop);
}

std::vector<Articulation> cymbalArticulations (Layers edge, Layers bow, Layers choke,
                                               std::pair<double, double> releaseBottom, std::pair<double, double> releaseTop)
{
    std::vector<std::pair<juce::String, Layers>> zones;
    zones.emplace_back ("Edge", std::move (edge));
    zones.emplace_back ("Bow", std::move (bow));
    return cymbalZones (std::move (zones), { "ride edge", "ride bow" }, std::move (choke), releaseBottom, releaseTop);
}

const juce::StringArray& hatOpenness()
{
    static const juce::StringArray names { "Closed", "1/4 Open", "2/4 Open", "3/4 Open", "Open" };
    return names;
}

HatLayout hatArticulations (std::vector<Layers> tip, std::vector<Layers> shank, std::vector<Layers> edge,
                            Layers pedal, Layers footsplash, Layers transition)
{
    using Range = std::pair<double, double>;
    struct Release { Range bottom, top; };
    const Release hatShort { { 0.5, 0.3 }, { 1.0, 1.5 } };
    // Release ranges per openness step, getting longer as the hat opens.
    const Release release[] = { hatShort, hatShort, hatShort, { { 0.5, 0.3 }, { 2.0, 3.0 } }, { { 0.5, 0.3 }, { 2.0, 4.0 } } };

    struct Group
    {
        const char* label;
        const char* key;
        std::vector<Layers>& steps;
        int zone, firstId;
        std::vector<int> notes;
    };
    Group groups[] = { { "Tip", "tip", tip, hatTip, 5, { 42, 61, 62, 63, 64 } },
                       { "Shank", "shank", shank, hatShank, 0, { 22, 66, 67, 68, 69 } },
                       { "", "edge", edge, hatEdge, 12, { 108, 109, 110, 111, 112 } } };

    HatLayout out;
    for (auto& g : groups)
    {
        if ((int) g.steps.size() != hatOpennessSteps)
            throw Error (std::string ("hi-hat ") + g.key + ": expected 5 openness steps");
        ControllerZone ctrl;
        ctrl.zoneType = g.zone;
        for (int k = 0; k < hatOpennessSteps; ++k)
        {
            juce::StringArray words { "Hi-Hat", g.label, hatOpenness()[k] };
            words.removeEmptyStrings();
            Articulation a;
            a.name = words.joinIntoString (" ");
            a.layers = std::move (g.steps[(size_t) k]);
            a.releaseBottom = release[k].bottom;
            a.releaseTop = release[k].top;
            a.artid = g.firstId + k;
            a.noteon = g.notes[(size_t) k];
            a.mute = k < 2;
            out.articulations.push_back (std::move (a));
            ctrl.artids.push_back (g.firstId + k);
        }
        out.controllerZones.push_back (std::move (ctrl));
    }

    auto add = [&] (const char* name, std::optional<int> zone, Layers layers, Release r, int artid, int note, bool mute)
    {
        Articulation a;
        a.name = name;
        a.zoneType = zone;
        a.layers = std::move (layers);
        a.releaseBottom = r.bottom;
        a.releaseTop = r.top;
        a.artid = artid;
        a.noteon = note;
        a.mute = mute;
        out.articulations.push_back (std::move (a));
    };
    add ("Hi-Hat Pedal", (int) hatPedal, std::move (pedal), hatShort, 10, 44, true);
    add ("Hi-Hat Footsplash", (int) hatFootsplash, std::move (footsplash), { { 0.5, 0.3 }, { 2.0, 4.0 } }, 11, 70, true);
    if (! transition.empty())
        add ("Hi-Hat Transition", std::nullopt, std::move (transition), { { 0.5, 0.3 }, { 2.0, 3.0 } }, 17, 23, false);

    std::stable_sort (out.articulations.begin(), out.articulations.end(),
                      [] (const Articulation& a, const Articulation& b) { return *a.artid < *b.artid; });
    return out;
}

std::vector<double> makeChoke (const std::vector<double>& samples, int rate, double length, double fadeIn, double fadeOut)
{
    const auto n = samples.size();
    if (n == 0)
        return {};
    // 5 ms average of |x|, accumulated exactly as mci.py does so the cut lands on the same frame.
    const auto win = (size_t) std::max (1, (int) (rate * 0.005));
    std::vector<double> env (n);
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        acc += std::abs (samples[i]);
        if (i >= win)
            acc -= std::abs (samples[i - win]);
        env[i] = acc / (double) std::min (i + 1, win);
    }
    const auto peak = (size_t) (std::max_element (env.begin(), env.end()) - env.begin());   // the first maximum
    auto start = std::min (n - 1, peak + (size_t) (int) (rate * 0.02));
    while (start < n - 1 && env[start] > env[peak] * 0.5)
        ++start;

    const auto keep = (size_t) std::nearbyint (length * rate);
    const auto m = std::min (keep, n - start);
    const auto fi = (double) std::max (1, (int) (fadeIn * rate));
    const auto fo = (double) std::max (1, (int) (fadeOut * rate));
    std::vector<double> out (m);
    for (size_t i = 0; i < m; ++i)
    {
        const double g = std::min (1.0, (double) i / fi)
                       * std::min (1.0, (double) m > fo ? (double) (m - 1 - i) / fo : 1.0);
        out[i] = samples[start + i] * g;
    }
    return out;
}

Samples quantize (const float* samples, size_t count)
{
    Samples out (count);
    for (size_t i = 0; i < count; ++i)
        out[i] = juce::jlimit (-32768, 32767, roundHalfEven ((double) samples[i] * 32768.0)) * 256;
    return out;
}

Samples quantize (const std::vector<double>& samples)
{
    Samples out (samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        out[i] = juce::jlimit (-32768, 32767, roundHalfEven (samples[i] * 32768.0)) * 256;
    return out;
}

std::vector<int> velocityThresholds (const std::vector<double>& loudness)
{
    const int n = (int) loudness.size();
    double top = 0.0;
    for (auto level : loudness)
        top = std::max (top, level);
    if (top <= 0.0)
        top = 1.0;   // a zone with no layers (empty Choke Ring) is fine
    std::vector<int> out ((size_t) n, 0);
    for (int i = 1; i < n; ++i)
        out[(size_t) i] = roundHalfEven (127.0 * std::sqrt (std::max (loudness[(size_t) i], 0.0) / top));
    for (int i = 1; i < n; ++i)               // strictly rising...
        out[(size_t) i] = std::max (out[(size_t) i], out[(size_t) i - 1] + 1);
    for (int i = n - 1; i > 0; --i)           // ...and room left for every layer above
    {
        out[(size_t) i] = std::min (out[(size_t) i], 127 - (n - 1 - i));
        if (i < n - 1)
            out[(size_t) i] = std::min (out[(size_t) i], out[(size_t) i + 1] - 1);
    }
    return out;
}

MciFile buildInstrument (const juce::String& name, const juce::String& instrumentType,
                         const std::vector<Articulation>& articulations, const juce::MemoryBlock& image,
                         const juce::String& library, const juce::String& mic,
                         double sampleVolume, const std::vector<ControllerZone>& controllerZones)
{
    MciFile f;
    auto put = [&f] (const std::string& key, Value v) { f.set (key, std::move (v)); };
    auto releaseArray = [] (double first)
    {
        std::vector<float> v ((size_t) rtArrayLen, 1.0f);
        v[0] = (float) first;
        return v;
    };

    std::vector<const Samples*> pool;
    std::vector<int> poolRoundRobin;     // round-robin position of every pool entry
    std::vector<std::vector<int>> envelopes;
    std::vector<std::pair<int, int>> zones;
    for (size_t a = 0; a < articulations.size(); ++a)
    {
        const auto& art = articulations[a];
        const auto P = "INST0artic" + std::to_string (a);
        const int artid = art.artid.value_or ((int) a);
        put (P + "artid", Value::ofInt (artid));
        put (P + "noteon", Value::ofInt (art.noteon));
        put (P + "numlay", Value::ofInt ((int) art.layers.size()));
        put (P + "vol", Value::ofFloat (1.0));
        put (P + "artn", Value::ofString (art.name));
        put (P + "chk", Value::ofU8 (art.choke ? 1 : 0));
        put (P + "fak", Value::ofU8 (0));
        put (P + "mut", Value::ofU8 (art.mute.value_or (art.choke) ? 1 : 0));
        put (P + "minrtb", Value::ofFloats (releaseArray (art.releaseBottom.first)));
        put (P + "minrtt", Value::ofFloats (releaseArray (art.releaseBottom.second)));
        put (P + "maxrtb", Value::ofFloats (releaseArray (art.releaseTop.first)));
        put (P + "maxrtt", Value::ofFloats (releaseArray (art.releaseTop.second)));
        if (art.zoneType.has_value())
            zones.emplace_back (*art.zoneType, artid);

        // Each layer's loudness is the mean of its round robins' envelope peaks.
        std::vector<double> loud;
        for (const auto& layer : art.layers)
        {
            juce::int64 sum = 0;
            for (const auto& s : layer)
            {
                envelopes.push_back (rmsEnvelope (s));
                if (envelopes.back().empty())
                    envelopes.back().push_back (0);
                sum += *std::max_element (envelopes.back().begin(), envelopes.back().end());
            }
            loud.push_back (layer.empty() ? 0.0 : (double) sum / (double) layer.size());
        }
        const auto minvel = art.minVelocities.empty() ? velocityThresholds (loud) : art.minVelocities;
        const auto nl = art.layers.size();
        for (size_t l = 0; l < nl; ++l)
        {
            const auto& layer = art.layers[l];
            if (layer.size() > (size_t) maxRoundRobins)
                throw Error (art.name.toStdString() + " layer " + std::to_string (l) + ": more than 16 round robins");
            if (l >= minvel.size())
                throw Error (art.name.toStdString() + ": fewer minimum velocities than layers");
            const auto L = P + "veloLay" + std::to_string (l);
            std::vector<int> idx;
            for (size_t r = 0; r < layer.size(); ++r)
            {
                idx.push_back ((int) pool.size());
                pool.push_back (&layer[r]);
                poolRoundRobin.push_back ((int) r);
            }
            idx.resize ((size_t) maxRoundRobins, -1);
            put (L + "nums", Value::ofInt ((int) layer.size()));
            put (L + "smpidx", Value::ofInts (idx));
            put (L + "minvel", Value::ofInt (minvel[l]));
            put (L + "minvol", Value::ofFloat (l == 0 ? 0.0 : 0.95));
            put (L + "maxvol", Value::ofFloat (1.0));
            const double t = nl > 1 ? (double) l / (double) (nl - 1) : 0.0;   // a single layer takes the bottom values
            const double lo = art.releaseBottom.first + t * (art.releaseBottom.second - art.releaseBottom.first);
            const double hi = art.releaseTop.first + t * (art.releaseTop.second - art.releaseTop.first);
            put (L + "minrtl", Value::ofFloats (releaseArray (lo)));
            put (L + "maxrtl", Value::ofFloats (releaseArray (hi)));
        }
    }

    // Pool: compress every sample, then lay out the RAM heads, first round robins first.
    juce::MemoryOutputStream data;
    std::vector<HeadLayout> layouts;
    std::vector<int> dlen;
    for (const auto* s : pool)
    {
        const auto before = data.getDataSize();
        layouts.push_back (headLayout (encodeBlocks (*s, data)));
        dlen.push_back ((int) (data.getDataSize() - before));
    }
    std::vector<size_t> headOrder (pool.size());
    std::iota (headOrder.begin(), headOrder.end(), size_t (0));
    std::stable_sort (headOrder.begin(), headOrder.end(),
                      [&] (size_t a, size_t b) { return poolRoundRobin[a] < poolRoundRobin[b]; });
    std::vector<int> dedo (pool.size(), 0);
    juce::MemoryOutputStream heads;
    for (auto i : headOrder)
    {
        dedo[i] = (int) heads.getDataSize();
        encodeHead (*pool[i], layouts[i].desc, heads);
    }
    std::vector<int> dofs;
    auto pos = (juce::int64) heads.getDataSize();
    for (auto n : dlen)
    {
        if (pos > std::numeric_limits<int>::max())
            throw Error ("the instrument is too large for a .mci file");
        dofs.push_back ((int) pos);
        pos += n;
    }

    const std::string Q = "INST0pool0";
    std::vector<int> fcnt, desc, nobu;
    for (size_t i = 0; i < pool.size(); ++i)
    {
        fcnt.push_back ((int) pool[i]->size());
        desc.push_back (layouts[i].desc);
        nobu.push_back (layouts[i].nobu);
    }
    put (Q + "psz", Value::ofInt ((int) pool.size()));
    put (Q + "dofs", Value::ofInts (dofs));
    put (Q + "dlen", Value::ofInts (dlen));
    put (Q + "fcnt", Value::ofInts (fcnt));
    put (Q + "nchn", Value::ofInts (std::vector<int> (pool.size(), 1)));
    put (Q + "dedo", Value::ofInts (dedo));
    put (Q + "desc", Value::ofInts (desc));
    put (Q + "nobu", Value::ofInts (nobu));
    put (Q + "svol", Value::ofFloats (std::vector<float> (pool.size(), (float) sampleVolume)));
    for (size_t i = 0; i < pool.size(); ++i)
    {
        const auto& env = envelopes[i];
        const auto R = Q + "rmsenv" + std::to_string (i);
        put (R + "rmse", Value::ofInts (env));
        put (R + "rmsl", Value::ofInt ((int) env.size()));
        put (R + "mrms", Value::ofFloat ((double) *std::max_element (env.begin(), env.end())));
    }

    for (size_t z = 0; z < zones.size(); ++z)
    {
        put ("INST0zoneArtId" + std::to_string (z), Value::ofInt (zones[z].second));
        put ("INST0zoneType" + std::to_string (z), Value::ofInt (zones[z].first));
    }
    for (size_t c = 0; c < controllerZones.size(); ++c)
    {
        const auto C = "INST0hctrlzn" + std::to_string (c);
        put (C + "ctrzone", Value::ofInt (controllerZones[c].zoneType));
        put (C + "stznsz", Value::ofInt ((int) controllerZones[c].artids.size()));
        put (C + "trzone", Value::ofInts (controllerZones[c].artids));
    }

    // Instrument-wide values, as written by the Editor.
    const auto [micpos, mict] = micDefaults (instrumentType);
    for (const auto& [k, v] : std::initializer_list<std::pair<const char*, int>> {
             { "INST0chkn", 0 }, { "INST0ecov", 0 }, { "INST0insv", 1 }, { "INST0micnt", 1 },
             { "INST0numart", (int) articulations.size() }, { "INST0nznctrl", (int) controllerZones.size() },
             { "INST0zonecnt", (int) zones.size() }, { "INST0micinf0micpos", micpos }, { "INST0micinf0mict", mict } })
        put (k, Value::ofInt (v));
    for (const auto& [k, v] : std::initializer_list<std::pair<const char*, double>> {
             { "att", 0.0 }, { "att2", 0.0 }, { "dmp", 0.0 }, { "dmp2", 0.0 }, { "micp", 0.5 }, { "micpz", 1.0 },
             { "micrs", 0.0 }, { "mictn", 0.5 }, { "micv", 1.0 }, { "rel", 1.0 }, { "rel2", 1.0 },
             { "sus", 1.0 }, { "sus2", 1.0 } })
        put (std::string ("INST0micinf0") + k, Value::ofFloat (v));
    for (const auto& [k, v] : std::initializer_list<std::pair<const char*, int>> {
             { "INST0dav2", 1 }, { "INST0micinf0enven", 1 }, { "INST0micinf0isst", 0 },
             { "INST0micinf0micen", 1 }, { "INST0micinf0micm", 0 }, { "INST0tmr", 1 },
             { "INSTedtr", 1 }, { "INSTmnlth", 1 } })
        put (k, Value::ofU8 (v));
    put ("INST0insdat", Value::ofString ({}));
    put ("INST0insimj", Value::ofString ({}));
    put ("INST0insnam", Value::ofString (name));
    put ("INST0instyp", Value::ofString (instrumentType));
    put ("INST0libnam", Value::ofString (library));
    put ("INST0micinf0micn", Value::ofString (mic.isNotEmpty() ? mic : instrumentType));

    f.image = image;
    f.heads = heads.getMemoryBlock();
    f.data = data.getMemoryBlock();
    return f;
}
} // namespace emk::mci
