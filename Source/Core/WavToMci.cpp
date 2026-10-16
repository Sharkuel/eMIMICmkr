#include "WavToMci.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>

namespace emk
{
namespace
{
using mci::Error;

/** Thrown when keepGoing asks to stop. */
struct Stopped {};

int roundHalfEven (double x)   // Python's round()
{
    return (int) std::nearbyint (x);
}

juce::int64 floorDiv (juce::int64 a, juce::int64 b)   // Python's //
{
    const auto q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

juce::String quoted (const juce::String& s)   // Python's repr() of a plain string
{
    return s.containsChar ('\'') && ! s.containsChar ('"') ? "\"" + s + "\"" : "'" + s + "'";
}

juce::StringArray splitWords (const juce::String& s)   // Python's str.split()
{
    auto words = juce::StringArray::fromTokens (s, " \t\n\r\f\v", "");
    words.removeEmptyStrings();
    return words;
}

bool allDigits (const juce::String& s)
{
    return s.isNotEmpty() && s.containsOnly ("0123456789");
}

juce::String stripTrailingDigits (const juce::String& s)
{
    auto end = s.length();
    while (end > 0 && juce::CharacterFunctions::isDigit (s[end - 1]))
        --end;
    return s.substring (0, end);
}

/** Parses "<piece> - <ZONE> - <hit>v<velocity>.wav" as wav2mci.py's HIT_NAME does: the
    shortest piece for which the rest matches, a zone without '-', case-insensitive "v.wav". */
bool parseHitName (const juce::String& base, juce::String& piece, juce::String& zone, int& velocity)
{
    for (int i = base.indexOf (1, " - "); i > 0; i = base.indexOf (i + 1, " - "))
    {
        const auto rest = base.substring (i + 3);
        const int dash = rest.indexOfChar ('-');
        if (dash < 2 || rest[dash - 1] != ' ' || rest[dash + 1] != ' ')
            continue;
        const auto tail = rest.substring (dash + 2);
        if (! tail.endsWithIgnoreCase (".wav"))
            continue;
        const auto numbers = tail.dropLastCharacters (4);
        const int v = numbers.indexOfIgnoreCase ("v");
        if (v < 0 || ! allDigits (numbers.substring (0, v)) || ! allDigits (numbers.substring (v + 1)))
            continue;
        piece = base.substring (0, i);
        zone = rest.substring (0, dash - 1);
        velocity = numbers.substring (v + 1).getIntValue();
        return true;
    }
    return false;
}

juce::uint32 crc32 (const juce::uint8* p, size_t n, juce::uint32 crc = 0)
{
    static const auto table = []
    {
        std::array<juce::uint32, 256> t {};
        for (juce::uint32 i = 0; i < 256; ++i)
        {
            auto c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void writeBigEndian (juce::MemoryOutputStream& out, juce::uint32 v)
{
    out.writeIntBigEndian ((int) v);
}

void pngChunk (juce::MemoryOutputStream& out, const char* kind, const juce::MemoryBlock& data)
{
    juce::MemoryBlock body (kind, 4);
    body.append (data.getData(), data.getSize());
    writeBigEndian (out, (juce::uint32) data.getSize());
    out << body;
    writeBigEndian (out, crc32 (static_cast<const juce::uint8*> (body.getData()), body.getSize()));
}

/** Hits sorted softest first; equal velocities keep their order, as Python's sorted() does. */
WavToMci::Hits byVelocity (WavToMci::Hits hits)
{
    std::stable_sort (hits.begin(), hits.end(),
                      [] (const WavToMci::Hit& a, const WavToMci::Hit& b) { return a.velocity < b.velocity; });
    return hits;
}

/** The first hardest hit, as Python's max() picks it. */
const WavToMci::Hit& hardest (const WavToMci::Hits& hits)
{
    size_t best = 0;
    for (size_t i = 1; i < hits.size(); ++i)
        if (hits[i].velocity > hits[best].velocity)
            best = i;
    return hits[best];
}

int shareOf (const WavToMci::Shares& shares, const juce::String& key)
{
    for (const auto& [k, v] : shares)
        if (k == key)
            return v;
    return 0;
}

void walk (const juce::File& root, const juce::File& dir, juce::StringArray& out)
{
    // os.walk order: a folder's files in directory order, then each subfolder in turn.
    juce::Array<juce::File> folders;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
    {
        if (entry.isDirectory())
        {
            if (! entry.getFile().isSymbolicLink())
                folders.add (entry.getFile());
        }
        else
        {
            out.add (entry.getFile().getRelativePathFrom (root));
        }
    }
    for (const auto& f : folders)
        walk (root, f, out);
}
} // namespace

//==============================================================================
int WavToMci::cellLimit (const juce::String& type)
{
    // Wave cells per instrument, shared by its zones. The Mimic preloads about 0.1 MB of RAM
    // per cell whatever the hit length, and kits built to the Editor's limits (snare 300,
    // toms 200...) glitched once several were loaded, while Pearl's own instruments play clean.
    // So these follow Pearl's factory library (2026-10-13): kicks 12-45 cells, snares 84-140,
    // toms 49-70, crashes/splashes/chinas 32-65, rides 74-112, percussion 30. Cymbals and
    // rides keep one cell for the choke ring. Hi-hats: see hatHits.
    if (type == "Kick")
        return 48;
    if (type == "Snare")
        return 140;
    if (type == "Tom")
        return 70;
    if (type == "Cymbal" || type == "Perc")
        return 64;
    if (type == "Ride")
        return 112;
    return 0;
}

juce::File WavToMci::commonFolder (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return {};
    for (auto folder = files.getFirst();;)
    {
        if (std::all_of (files.begin(), files.end(), [&] (const juce::File& f) { return f == folder || f.isAChildOf (folder); }))
            return folder;
        const auto parent = folder.getParentDirectory();
        if (parent == folder)
            return {};
        folder = parent;
    }
}

WavToMci::Input::Input (const juce::File& file) : Input (juce::Array<juce::File> { file })
{
}

WavToMci::Input::Input (const juce::Array<juce::File>& files)
{
    if (files.size() == 1)
    {
        const auto& file = root = files.getFirst();
        if (file.isDirectory())
        {
            walk (file, file, entries);
            valid = true;
        }
        else if (file.existsAsFile())
        {
            zip = std::make_unique<juce::ZipFile> (file);
            for (int i = 0; i < zip->getNumEntries(); ++i)
                if (! zip->getEntry (i)->filename.endsWithChar ('/'))
                    entries.add (zip->getEntry (i)->filename);
            valid = zip->getNumEntries() > 0;
        }
        return;
    }

    // Several inputs read as one: every file under each folder, named relative to the folder
    // holding them all, so a ride's BOW, BELL and EDGE folders read like their parent.
    juce::Array<juce::File> folders;
    for (const auto& f : files)
    {
        if (f.isDirectory())
            folders.add (f);
        else
            skippedInputs.add (f.getFullPathName() + ": "
                               + (f.existsAsFile() && (f.hasFileExtension ("zip") || juce::ZipFile (f).getNumEntries() > 0)
                                      ? "a .zip is converted on its own, not with other inputs"
                                      : "not a folder"));
    }
    root = commonFolder (folders);
    if (folders.isEmpty() || root == juce::File())
        return;
    std::set<juce::String> seen;
    for (const auto& folder : folders)
    {
        juce::StringArray names;
        walk (root, folder, names);
        for (const auto& n : names)
            if (seen.insert (n).second)   // a folder given inside another one
                entries.add (n);
    }
    valid = true;
}

WavToMci::Input::~Input() = default;

juce::MemoryBlock WavToMci::Input::read (const juce::String& name) const
{
    juce::MemoryBlock data;
    if (zip != nullptr)
    {
        const std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (zip->getIndexOfFileName (name)));
        if (in == nullptr)
            throw Error ("cannot read " + name.toStdString());
        in->readIntoMemoryBlock (data);
    }
    else if (! root.getChildFile (name).loadFileAsData (data))
    {
        throw Error ("cannot read " + name.toStdString());
    }
    return data;
}

bool WavToMci::Input::has (const juce::String& name) const
{
    if (entries.contains (name))
        return true;
    // Several folders given together leave out the folder holding them, where a piece's
    // mimic.txt sits.
    return zip == nullptr && root != juce::File() && root.getChildFile (name).existsAsFile();
}

juce::String WavToMci::Input::topFolder() const
{
    juce::StringArray tops;
    for (auto n : entries)
    {
        n = n.replaceCharacter ('\\', '/');
        if (n.containsChar ('/'))
            tops.addIfNotAlreadyThere (n.upToFirstOccurrenceOf ("/", false, false));
    }
    return tops.size() == 1 ? tops[0] : juce::String();
}

const WavToMci::Hits* WavToMci::Piece::find (const juce::String& zone) const
{
    for (const auto& [k, hits] : zones)
        if (k == zone)
            return &hits;
    return nullptr;
}

WavToMci::Hits& WavToMci::Piece::add (const juce::String& zone)
{
    for (auto& [k, hits] : zones)
        if (k == zone)
            return hits;
    zones.emplace_back (zone, Hits());
    return zones.back().second;
}

//==============================================================================
juce::String WavToMci::classifyHat (const juce::String& zoneLabel)
{
    static const juce::StringArray heads { "HATS", "HAT", "HIHAT", "HIHATS", "HH" };
    const auto words = splitWords (zoneLabel.toUpperCase().replaceCharacter ('-', ' '));
    if (words.isEmpty() || ! heads.contains (stripTrailingDigits (words[0])))
        return {};
    juce::StringArray rest (words);
    rest.remove (0);
    if (rest[0] == "PEDAL")
        return "pedal";
    if ((rest[0] == "FOOT" && rest[1] == "SPLASH") || rest[0] == "FOOTSPLASH" || rest[0] == "SPLASH")
        return "footsplash";
    if (rest.size() >= 2 && juce::StringArray { "BOW", "TIP", "EDGE", "SHANK", "BELL" }.contains (rest[0]))
    {
        const auto area = rest[0].toLowerCase();
        rest.remove (0);
        return area + ":" + rest.joinIntoString ("").toLowerCase();
    }
    throw Error (("unknown hi-hat zone " + quoted (zoneLabel)).toStdString());
}

double WavToMci::opennessRank (const juce::String& level)
{
    // TIGHT < CLOSED < CLOSED2 < OPEN0 < OPEN1 < ...; unknown words sort last.
    for (auto word : { "tight", "closed", "half", "halfopen", "open" })
    {
        if (! level.startsWith (word))
            continue;
        const auto digits = level.substring ((int) std::strlen (word));
        if (digits.isNotEmpty() && ! allDigits (digits))
            continue;
        const int n = digits.getIntValue();
        const juce::String w (word);
        if (w == "tight")
            return -2.0;
        if (w == "closed")
            return -1.0 + n / 100.0;
        if (w == "open")
            return (double) n;
        return 2.5;
    }
    return 1000.0;
}

juce::StringArray WavToMci::pickOpenness (juce::StringArray levels, int steps)
{
    // Five levels from closed to open. TIGHT stands in only when there is no CLOSED.
    std::vector<juce::String> sorted (levels.begin(), levels.end());
    std::stable_sort (sorted.begin(), sorted.end(),
                      [] (const juce::String& a, const juce::String& b) { return opennessRank (a) < opennessRank (b); });
    const bool hasClosed = std::any_of (sorted.begin(), sorted.end(), [] (const juce::String& l) { return l.startsWith ("closed"); });
    if ((int) sorted.size() > steps && sorted.front() == "tight" && hasClosed)
        sorted.erase (sorted.begin());

    juce::StringArray out;
    const int n = (int) sorted.size();
    if (n == 0)
        return out;
    for (int i = 0; i < steps; ++i)
    {
        if (n >= steps)
            out.add (sorted[(size_t) roundHalfEven ((double) (i * (n - 1)) / (double) (steps - 1))]);
        else
            out.add (sorted[(size_t) std::min (i, n - 1)]);   // repeat the most open
    }
    return out;
}

std::pair<juce::String, juce::String> WavToMci::classify (const juce::String& zoneLabel)
{
    const auto hat = classifyHat (zoneLabel);
    if (hat.isNotEmpty())
        return { "hat", hat };

    juce::String upper = zoneLabel.toUpperCase(), spaced;
    for (int i = 0; i < upper.length();)
    {
        if (juce::CharacterFunctions::isDigit (upper[i]))
        {
            while (i < upper.length() && juce::CharacterFunctions::isDigit (upper[i]))
                ++i;
            spaced << " ";
        }
        else
        {
            spaced << juce::String::charToString (upper[i++]);
        }
    }
    const auto words = splitWords (spaced);
    if (words.isEmpty())
        throw Error (("cannot read zone " + quoted (zoneLabel)).toStdString());

    const auto& head = words[0];
    if (head == "KICK" || head == "BD" || head == "BASSDRUM")
        return { "kick", "kick" };
    static const juce::StringArray percWords { "PERC", "PERCUSSION", "OCTOBAN", "COWBELL", "TAMBOURINE", "SHAKER", "CLAP",
                                               "BLOCK", "WOODBLOCK", "CONGA", "BONGO", "TIMBALE", "TRIANGLE", "CHIMES", "AUX" };
    if (percWords.contains (head))
    {
        // Main, Secondary, Additional 1 and 2 when the label says which; otherwise the label
        // itself, and the zones fill the four in label order ("PERC 1 HEAD", "PERC 2 HEAD").
        const auto squashed = upper.removeCharacters (" _");
        if (squashed.endsWith ("ADD2") || squashed.endsWith ("ADDITIONAL2")) return { "perc", "add2" };
        if (squashed.endsWith ("ADD1") || squashed.endsWith ("ADDITIONAL1")) return { "perc", "add1" };
        if (words.contains ("SECONDARY")) return { "perc", "secondary" };
        if (words.contains ("MAIN")) return { "perc", "main" };
        return { "perc", "z:" + upper };
    }
    static const juce::StringArray cymbalWords { "RIDE", "CRASH", "SPLASH", "CHINA", "STACK", "CYMBAL", "BELL" };
    const juce::String family = cymbalWords.contains (head)                                    ? "cymbal"
                              : (head == "SNARE" || head == "SD")                              ? "snare"
                              : (head == "TOM" || head == "FLOORTOM" || head == "RACKTOM")     ? "tom"
                                                                                               : "";
    if (family.isEmpty())
        throw Error (("unknown instrument in zone " + quoted (zoneLabel)).toStdString());

    static const std::map<juce::String, juce::String> zoneWords {
        { "BOW", "bow" }, { "BELL", "bell" }, { "EDGE", "edge" }, { "CHOKE", "choke" },
        { "CENTER", "center" }, { "CENTRE", "center" }, { "HEAD", "center" }, { "HIT", "center" },
        { "RIMSHOT", "rimshot" }, { "RIM", "rimshot" }, { "RIMCLICK", "rimclick" }, { "CROSSSTICK", "sidestick" },
        { "SIDESTICK", "sidestick" }, { "XSTICK", "sidestick" }
    };
    if ((family == "snare" || family == "tom") && words.contains ("OFF"))
        return { family, "offcenter" };   // an off-center head hit: no Mimic zone, left out
    juce::String zone;
    for (int i = 1; i < words.size() && zone.isEmpty(); ++i)
        if (const auto it = zoneWords.find (words[i]); it != zoneWords.end())
            zone = it->second;
    // A shallow rimshot, which the Mimic's snare plays from its Rim Shot Side zone.
    if (family == "snare" && (words.contains ("SHALLOW") || (zone == "rimshot" && words.contains ("SIDE"))))
        return { family, "rimshotside" };
    if (family == "snare" && zone == "edge")
        return { family, "side" };        // a hit near the edge of the head: the Mimic's Side zone
    return { family, zone.isNotEmpty() ? zone : family == "cymbal" ? "bow" : "center" };
}

std::vector<WavToMci::Piece> WavToMci::scan (const Input& src, juce::StringArray& skipped)
{
    std::vector<Piece> pieces;
    skipped.addArray (src.skipped());
    for (const auto& name : src.names())
    {
        const auto base = name.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
        juce::String pieceName, zoneLabel;
        int velocity = 0;
        if (! parseHitName (base, pieceName, zoneLabel, velocity))
        {
            if (base.endsWithIgnoreCase (".wav"))
                skipped.add (name + ": name not in '<piece> - <ZONE> - <hit>v<velocity>.wav' form");
            continue;
        }
        std::pair<juce::String, juce::String> kind;
        try
        {
            kind = classify (zoneLabel);
        }
        catch (const Error& e)
        {
            skipped.add (name + ": " + e.what());
            continue;
        }
        auto it = std::find_if (pieces.begin(), pieces.end(), [&] (const Piece& p) { return p.name == pieceName; });
        if (it == pieces.end())
        {
            pieces.push_back ({ pieceName, kind.first, {} });
            it = pieces.end() - 1;
        }
        it->add (kind.second).push_back ({ velocity, name, {} });
    }
    return pieces;
}

WavToMci::Shares WavToMci::shareCells (int budget, const Shares& available)
{
    Shares out, left (available);
    juce::int64 remaining = budget;
    while (! left.empty())
    {
        const auto each = floorDiv (remaining, (juce::int64) left.size());
        Shares small, rest;
        for (const auto& kv : left)
            (kv.second <= each ? small : rest).push_back (kv);
        if (small.empty())
        {
            const auto extra = remaining - each * (juce::int64) left.size();
            for (size_t i = 0; i < left.size(); ++i)
                out.emplace_back (left[i].first, (int) (each + ((juce::int64) i < extra ? 1 : 0)));
            break;
        }
        for (const auto& kv : small)
        {
            out.push_back (kv);
            remaining -= kv.second;
        }
        left = rest;
    }
    return out;
}

std::vector<WavToMci::Hits> WavToMci::splitLayers (Hits hits, int layersWanted)
{
    // Softest to hardest, as even as possible, every layer at least one hit.
    hits = byVelocity (std::move (hits));
    const int total = (int) hits.size();
    const int n = std::min (layersWanted, total);
    std::vector<Hits> out;
    for (int i = 0; i < n; ++i)
    {
        const auto from = roundHalfEven ((double) (i * total) / (double) n);
        const auto to = roundHalfEven ((double) ((i + 1) * total) / (double) n);
        out.emplace_back (hits.begin() + from, hits.begin() + to);
    }
    return out;
}

int WavToMci::roundRobins (const juce::String& family, const juce::String& zone)
{
    if ((zone == "center" || zone == "side" || zone == "rimshot" || zone == "rimshotside") && (family == "snare" || family == "tom"))
        return family == "snare" ? 6 : 5;
    return 4;
}

WavToMci::Hits WavToMci::evenSpread (Hits hits, int count)
{
    hits = byVelocity (std::move (hits));
    Hits picked;
    if (count == 1)
        picked.push_back (hits.back());
    else
        for (int i = 0; i < count; ++i)
            picked.push_back (hits[(size_t) roundHalfEven ((double) (i * ((int) hits.size() - 1)) / (double) (count - 1))]);
    return picked;
}

std::vector<WavToMci::Hits> WavToMci::roundRobinLayers (Hits hits, int rr)
{
    const auto n = (int) hits.size();
    const int layers = std::min (maxLayers, std::max (1, roundHalfEven ((double) n / (double) rr)));
    if (n > layers * maxRoundRobins)
        hits = evenSpread (std::move (hits), layers * maxRoundRobins);
    return splitLayers (std::move (hits), layers);
}

std::vector<int> WavToMci::minVelocities (const std::vector<Hits>& layers)
{
    std::vector<int> out { 0 };
    for (size_t i = 1; i < layers.size(); ++i)
        out.push_back (layers[i].front().velocity);
    for (size_t i = 1; i < out.size(); ++i)   // strictly rising, as the Mimic expects
        out[i] = std::min (127, std::max (out[i], out[i - 1] + 1));
    return out;
}

juce::MemoryBlock WavToMci::thumbnail (int width, int height)
{
    // A plain dark PNG, the size of the Editor's thumbnails.
    juce::MemoryBlock rows;
    for (int y = 0; y < height; ++y)
    {
        rows.append ("\0", 1);
        for (int x = 0; x < width; ++x)
            rows.append ("\x28\x2c\x34", 3);   // 40, 44, 52
    }
    juce::MemoryOutputStream idat;
    {
        juce::GZIPCompressorOutputStream z (idat);   // zlib's default level, like zlib.compress()
        z << rows;
    }

    juce::MemoryOutputStream ihdr;
    writeBigEndian (ihdr, (juce::uint32) width);
    writeBigEndian (ihdr, (juce::uint32) height);
    for (int b : { 8, 2, 0, 0, 0 })
        ihdr.writeByte ((char) b);

    juce::MemoryOutputStream png;
    png.write ("\x89PNG\r\n\x1a\n", 8);
    pngChunk (png, "IHDR", ihdr.getMemoryBlock());
    pngChunk (png, "IDAT", idat.getMemoryBlock());
    pngChunk (png, "IEND", {});
    return png.getMemoryBlock();
}

juce::String WavToMci::infoText (const PieceInfo& info)
{
    juce::String text;
    if (info.name.isNotEmpty())
        text << "name: " << info.name << "\n";
    if (info.library.isNotEmpty())
        text << "library: " << info.library << "\n";
    return text;
}

WavToMci::PieceInfo WavToMci::pieceInfo (const Piece& piece, const Input& src)
{
    PieceInfo info;
    juce::StringArray folders;
    for (const auto& [zone, hits] : piece.zones)
        if (! hits.empty())
        {
            const auto path = hits.front().path.replaceCharacter ('\\', '/');
            const auto zoneFolder = path.upToLastOccurrenceOf ("/", false, false);
            if (zoneFolder != path)
            {
                const auto pieceFolder = zoneFolder.upToLastOccurrenceOf ("/", false, false);
                folders.addIfNotAlreadyThere (pieceFolder != zoneFolder ? pieceFolder + "/" : juce::String());
                folders.addIfNotAlreadyThere (zoneFolder + "/");
            }
        }
    auto find = [&] (const juce::String& file) -> juce::String
    {
        for (const auto& folder : folders)
            for (const auto& name : { folder + file, (folder + file).replaceCharacter ('/', '\\') })
                if (src.has (name))
                    return name;
        return {};
    };
    if (const auto txt = find (infoFile); txt.isNotEmpty())
    {
        const auto block = src.read (txt);
        for (auto line : juce::StringArray::fromLines (juce::String::fromUTF8 ((const char*) block.getData(), (int) block.getSize())))
        {
            const auto key = line.upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
            const auto value = line.fromFirstOccurrenceOf (":", false, false).trim();
            if (key == "name") info.name = value;
            if (key == "library") info.library = value;
        }
    }
    if (const auto png = find (imageFile); png.isNotEmpty())
    {
        auto block = src.read (png);
        if (block.getSize() > 8 && std::memcmp (block.getData(), "\x89PNG", 4) == 0)
            info.image = std::move (block);
    }
    return info;
}

juce::String WavToMci::pieceTitle (const Piece& piece, const Input& src)
{
    // The zip's top folder when it names this piece (same size prefix), else the piece name
    // without the renderer's preset code ("_DEH9").
    auto shortName = piece.name;
    int end = shortName.length();
    while (end > 0 && juce::CharacterFunctions::isLetterOrDigit (shortName[end - 1]) && shortName[end - 1] < 128)
        --end;
    if (end < shortName.length() && end > 0 && shortName[end - 1] == '_')
        shortName = shortName.substring (0, end - 1);
    shortName = shortName.trim();

    const auto top = src.topFolder();
    const auto topWords = splitWords (top), shortWords = splitWords (shortName);
    if (top.isNotEmpty() && ! top.contains (" - ") && ! topWords.isEmpty() && ! shortWords.isEmpty()
        && topWords[0] == shortWords[0])
        return top;
    return shortName;
}

mci::Samples WavToMci::readWav (const juce::MemoryBlock& block)
{
    // Parsed by hand rather than through juce::WavAudioFormat, so the integers are exactly
    // those Python's wave module reads.
    auto* d = static_cast<const juce::uint8*> (block.getData());
    const auto size = block.getSize();
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        throw Error ("not a WAV file");

    int channels = 0, width = 0;
    juce::uint32 rate = 0;
    bool haveFormat = false, isFloat = false;
    const juce::uint8* raw = nullptr;
    size_t rawSize = 0;
    for (size_t pos = 12; pos + 8 <= size;)
    {
        const auto chunkSize = (size_t) juce::ByteOrder::littleEndianInt (d + pos + 4);
        const auto body = pos + 8;
        if (std::memcmp (d + pos, "fmt ", 4) == 0)
        {
            if (body + 16 > size || chunkSize < 16)
                throw Error ("WAV format chunk is too short");
            const auto tag = juce::ByteOrder::littleEndianShort (d + body);
            channels = juce::ByteOrder::littleEndianShort (d + body + 2);
            rate = juce::ByteOrder::littleEndianInt (d + body + 4);
            const int bits = juce::ByteOrder::littleEndianShort (d + body + 14);
            int format = tag;
            if (tag == 0xfffe)
            {
                // The sub-format GUID: its first two bytes are the format tag, the rest is fixed.
                static const juce::uint8 guid[] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
                if (body + 40 > size || std::memcmp (d + body + 26, guid, 14) != 0)
                    throw Error ("unknown WAV format (only integer PCM and 32-bit float are read)");
                format = juce::ByteOrder::littleEndianShort (d + body + 24);
            }
            if (format == 3)
            {
                if (bits != 32)
                    throw Error ("unsupported float sample width " + std::to_string (bits) + " bit");
                isFloat = true;
            }
            else if (format != 1)
            {
                throw Error ("unknown WAV format " + std::to_string (format) + " (only integer PCM and 32-bit float are read)");
            }
            width = (bits + 7) / 8;
            if (width == 0 || channels == 0)
                throw Error ("bad WAV sample width or channel count");
            haveFormat = true;
        }
        else if (std::memcmp (d + pos, "data", 4) == 0)
        {
            if (! haveFormat)
                throw Error ("WAV data chunk before fmt chunk");
            const auto frameSize = (size_t) (channels * width);
            const auto wanted = (chunkSize / frameSize) * frameSize;
            raw = d + body;
            rawSize = std::min (wanted, size - std::min (size, body));
            break;
        }
        pos = body + chunkSize + (chunkSize & 1);
    }
    if (raw == nullptr)
        throw Error ("WAV fmt chunk and/or data chunk missing");
    if (rate != (juce::uint32) sampleRate)
        throw Error (std::to_string (rate) + " Hz; render at 44100 Hz (resampling is not built yet)");
    if (width < 2 || width > 4)
        throw Error ("unsupported sample width " + std::to_string (width * 8) + " bit");

    std::vector<int> vals (rawSize / (size_t) width);
    for (size_t i = 0; i < vals.size(); ++i)
    {
        const auto* p = raw + i * (size_t) width;
        if (isFloat)
        {
            // May go past full scale; buildInstrument turns such an instrument down as a whole.
            float f;
            const auto bitsOf = juce::ByteOrder::littleEndianInt (p);
            std::memcpy (&f, &bitsOf, 4);
            // Kept within 2^30, so a wild value can't overflow (nor NaN stop the conversion).
            vals[i] = std::isnan (f) ? 0 : roundHalfEven (juce::jlimit (-1073741824.0, 1073741824.0, (double) f * 8388608.0));
        }
        else if (width == 2)
            vals[i] = (int) (juce::int16) juce::ByteOrder::littleEndianShort (p) * 256;
        else if (width == 3)
            vals[i] = juce::ByteOrder::littleEndian24Bit (p);   // sign-extended
        else
            vals[i] = (int) juce::ByteOrder::littleEndianInt (p) >> 8;
    }
    if (channels == 1)
        return vals;
    mci::Samples out;
    out.reserve (vals.size() / (size_t) channels + 1);
    for (size_t i = 0; i < vals.size(); i += (size_t) channels)
    {
        juce::int64 sum = 0;
        for (size_t c = i; c < std::min (vals.size(), i + (size_t) channels); ++c)
            sum += vals[c];
        out.push_back ((int) floorDiv (sum, channels));
    }
    return out;
}

//==============================================================================
static bool ringsOpen (const juce::String& family, const juce::String& zone)
{
    return family == "hat" && (zone.contains ("open") || zone == "footsplash");
}

// Percussion is cut like a short hit, but some of it rings like a cymbal or a tom: a tam-tam
// or gong rings 20-30 s, and SD3's timp-tom ("LeftFloor") is a drum filed under percussion
// (Alexandre's Area 33 Origin kit, 2026-10-16). Such pieces take that family's length.
static juce::String lengthFamily (const WavToMci::Piece& piece)
{
    if (piece.family != "perc")
        return piece.family;
    const auto name = piece.name.toUpperCase().removeCharacters (" _-");
    if (name.contains ("TAMTAM") || name.contains ("GONG"))
        return "cymbal";
    if (name.contains ("FLOOR") || name.contains ("TOM") || name.contains ("TIMP"))
        return "tom";
    return piece.family;
}

double WavToMci::maxSeconds (const juce::String& family, bool hasBell, const juce::String& zone)
{
    if (ringsOpen (family, zone))
        return 8.0;
    // Close to Pearl's factory hits: kicks 1.5-2.5 s, snares 1.5-3.5, toms 2-6. Cymbals ring
    // longer than Pearl's 12-14 s: SDSE keeps crashes up to 24 s and those play fine, and
    // length costs disk reads, not RAM.
    if (family == "kick")
        return 2.5;
    if (family == "snare" || family == "hat" || family == "perc")
        return 3.0;
    if (family == "tom")
        return 5.0;
    return family == "cymbal" && hasBell ? 20.0 : 16.0;
}

double WavToMci::fadeSeconds (const juce::String& family, bool hasBell, const juce::String& zone)
{
    return family == "cymbal" || ringsOpen (family, zone) ? maxSeconds (family, hasBell, zone) * 0.5 : shortFadeSeconds;
}

void WavToMci::fitLength (mci::Samples& x, int maxFrames, int fadeFrames)
{
    if ((int) x.size() <= maxFrames)
        return;
    x.resize ((size_t) maxFrames);
    const auto fade = std::min (fadeFrames, maxFrames);
    const auto start = maxFrames - fade;
    for (int i = 0; i < fade; ++i)
    {
        const auto left = (juce::int64) (fade - 1 - i);
        x[(size_t) (start + i)] = (int) floorDiv ((juce::int64) x[(size_t) (start + i)] * left * left, (juce::int64) fade * fade);
    }
}

mci::MciFile WavToMci::build (const Piece& piece, const Input& src, const juce::String& name, bool choke, bool hatAsRide,
                              const std::function<void (const juce::String&)>& log,
                              const std::function<void()>& onWavRead, bool fullLength,
                              int cellsPercent)
{
    // The instrument's wave-cell budget, cut to cellsPercent.
    auto cells = [cellsPercent] (const juce::String& kind) { return std::max (2, cellLimit (kind) * cellsPercent / 100); };
    const auto hatHitsUsed = std::max (1, hatHits * cellsPercent / 100);
    const bool pieceHasBell = piece.find ("bell") != nullptr;
    const auto ringsLike = lengthFamily (piece);
    auto readOne = [&] (const Hit& h)
    {
        if (onWavRead)
            onWavRead();
        try
        {
            auto x = readWav (src.read (h.path));
            if (h.blend.isEmpty())
                return x;
            auto y = readWav (src.read (h.blend));
            if (y.size() > x.size())
                std::swap (x, y);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (int) floorDiv ((juce::int64) x[i] + (i < y.size() ? y[i] : 0), 2);
            return x;
        }
        catch (const Error& e)
        {
            throw Error ((h.path + ": " + e.what()).toStdString());
        }
    };
    // No shallow (side) rimshots recorded: each rimshot is mixed with the rim click (or the
    // head hit) nearest its velocity, so the Mimic's Rim Shot Side zone isn't silent.
    Hits sideRimshots;
    juce::String sideFrom;
    if (piece.family == "snare" && piece.find ("rimshotside") == nullptr && piece.find ("rimshot") != nullptr)
    {
        sideFrom = piece.find ("rimclick") != nullptr ? "rimclick" : piece.find ("center") != nullptr ? "center" : "";
        if (sideFrom.isNotEmpty())
        {
            const auto& others = *piece.find (sideFrom);
            for (const auto& h : *piece.find ("rimshot"))
            {
                const Hit* nearest = &others.front();
                for (const auto& o : others)
                    if (std::abs (o.velocity - h.velocity) < std::abs (nearest->velocity - h.velocity))
                        nearest = &o;
                sideRimshots.push_back ({ h.velocity, h.path, nearest->path });
            }
        }
    }
    auto hitsOf = [&] (const juce::String& key) -> const Hits&
    {
        if (key == "rimshotside" && ! sideRimshots.empty())
            return sideRimshots;
        if (const auto* hits = piece.find (key))
            return *hits;
        throw Error ((piece.name + ": no " + key + " recordings").toStdString());
    };
    struct Zone
    {
        mci::Layers audio;
        std::vector<int> velocities;
    };
    // rr > 0: about rr round robins per layer (roundRobinLayers), else up to layers layers.
    auto zone = [&] (const juce::String& key, int layers = maxLayers, int maxHits = 0, int rr = 0)
    {
        auto hits = hitsOf (key);
        if (maxHits > 0 && (int) hits.size() > maxHits)   // keep an even spread of velocities
            hits = evenSpread (std::move (hits), maxHits);
        const auto split = rr > 0 ? roundRobinLayers (std::move (hits), rr) : splitLayers (std::move (hits), layers);
        const auto maxFrames = fullLength ? 0 : (int) std::lround (maxSeconds (ringsLike, pieceHasBell, key) * sampleRate);
        const auto fadeFrames = (int) std::lround (fadeSeconds (ringsLike, pieceHasBell, key) * sampleRate);
        Zone z;
        for (const auto& layer : split)
        {
            z.audio.emplace_back();
            for (const auto& h : layer)
            {
                z.audio.back().push_back (readOne (h));
                if (maxFrames > 0)
                    fitLength (z.audio.back().back(), maxFrames, fadeFrames);
            }
        }
        z.velocities = minVelocities (split);
        return z;
    };
    auto has = [&] (const juce::String& key) { return piece.find (key) != nullptr || (key == "rimshotside" && ! sideRimshots.empty()); };

    if (piece.family == "hat")
    {
        // SDSE hi-hat folders -> the factory Hat layout, or a ride-style 3-zone instrument.
        // Mapping (inferred, not from Pearl): the Mimic's tip zone plays BOW, its shank zone
        // plays EDGE, and its third (edge) controller zone plays BELL when there is one, else
        // EDGE again.
        std::vector<std::pair<juce::String, juce::StringArray>> areas;   // area -> levels, in folder order
        auto levelsOf = [&] (const juce::String& area) -> juce::StringArray*
        {
            for (auto& [a, levels] : areas)
                if (a == area)
                    return &levels;
            return nullptr;
        };
        for (const auto& [key, hits] : piece.zones)
        {
            if (! key.containsChar (':'))
                continue;
            auto area = key.upToFirstOccurrenceOf (":", false, false);
            area = area == "tip" ? "bow" : area == "shank" ? "edge" : area;
            if (levelsOf (area) == nullptr)
                areas.emplace_back (area, juce::StringArray());
            levelsOf (area)->add (key.fromFirstOccurrenceOf (":", false, false));
        }
        if (levelsOf ("bow") == nullptr && levelsOf ("edge") == nullptr)
            throw Error ((piece.name + ": a hi-hat needs BOW or EDGE folders").toStdString());
        const juce::String tipArea = levelsOf ("bow") != nullptr ? "bow" : "edge";
        const juce::String shankArea = levelsOf ("edge") != nullptr ? "edge" : "bow";
        const juce::String thirdArea = levelsOf ("bell") != nullptr ? "bell" : shankArea;

        // A TIP or SHANK folder counts as BOW or EDGE; find whichever spelling was recorded.
        auto keyFor = [&] (const juce::String& area, const juce::String& level)
        {
            for (const auto& alias : { area, area == "bow" ? juce::String ("tip") : area == "edge" ? juce::String ("shank") : area })
                if (has (alias + ":" + level))
                    return alias + ":" + level;
            return area + ":" + level;
        };

        if (hatAsRide)
        {
            auto closed = [&] (const juce::String& area) { return zone (keyFor (area, pickOpenness (*levelsOf (area))[0])); };
            auto bow = closed (tipArea), edge = closed (shankArea), bell = closed (thirdArea);
            auto arts = mci::rideArticulations (std::move (bow.audio), std::move (bell.audio), std::move (edge.audio));
            arts[0].minVelocities = bow.velocities;
            arts[1].minVelocities = bell.velocities;
            arts[2].minVelocities = edge.velocities;
            return mci::buildInstrument (name, "Ride", arts, thumbnail(), {}, "Ride");
        }

        auto steps = [&] (const juce::String& area)
        {
            const auto levels = pickOpenness (*levelsOf (area));
            log ("  " + name + ": " + area + " closed->open uses " + levels.joinIntoString (", ").toUpperCase());
            std::vector<mci::Layers> out;
            for (const auto& l : levels)
                out.push_back (zone (keyFor (area, l), hatLayers, hatHitsUsed).audio);
            return out;
        };
        auto tip = steps (tipArea);
        auto shank = steps (shankArea);
        auto edge = steps (thirdArea);
        mci::Layers pedal, splash;
        if (has ("pedal"))
            pedal = zone ("pedal", hatLayers, hatHitsUsed).audio;
        if (has ("footsplash"))
            splash = zone ("footsplash", hatLayers, hatHitsUsed).audio;
        if (pedal.empty())
            log ("  " + name + ": no pedal recording, that zone stays silent");
        if (splash.empty())
            log ("  " + name + ": no foot splash recording, that zone stays silent");
        const auto hat = mci::hatArticulations (std::move (tip), std::move (shank), std::move (edge),
                                                std::move (pedal), std::move (splash));
        return mci::buildInstrument (name, "Hat", hat.articulations, thumbnail(), {}, "Hi-Hat", 0.52, hat.controllerZones);
    }

    if (piece.family == "perc")
    {
        // Up to four zones; those not named Main, Secondary, Add1 or Add2 fill the gaps in label order.
        static const char* slots[] = { "main", "secondary", "add1", "add2" };
        static const char* zoneTypes[] = { "perc main", "perc secondary", "perc additional 1", "perc additional 2" };
        static const char* names[] = { "Main", "Secondary", "Additional 1", "Additional 2" };
        std::map<juce::String, juce::String> keyFor;   // slot -> zone key
        juce::StringArray loose;
        for (const auto& [key, hits] : piece.zones)
            if (key.startsWith ("z:"))
                loose.add (key);
            else
                keyFor[key] = key;
        loose.sort (true);
        for (const auto* slot : slots)
            if (keyFor.count (slot) == 0 && ! loose.isEmpty())
            {
                keyFor[slot] = loose[0];
                loose.remove (0);
            }
        for (const auto& extra : loose)
            log ("  " + name + ": the Mimic's percussion has four zones; leaving out " + extra.substring (2));
        Shares available;
        for (const auto* slot : slots)
            if (keyFor.count (slot) > 0)
                available.emplace_back (slot, (int) hitsOf (keyFor[slot]).size());
        const auto shares = shareCells (cells ("Perc"), available);
        std::vector<mci::Articulation> arts;
        for (int i = 0; i < 4; ++i)
        {
            if (keyFor.count (slots[i]) == 0)
                continue;
            auto z = zone (keyFor[slots[i]], maxLayers, shareOf (shares, slots[i]), roundRobins ("perc", slots[i]));
            mci::Articulation a;
            a.name = names[i];
            a.zoneType = mci::zoneType (zoneTypes[i]);
            a.layers = std::move (z.audio);
            a.minVelocities = z.velocities;
            arts.push_back (std::move (a));
        }
        return mci::buildInstrument (name, "Perc", arts, thumbnail(), {}, "Perc");
    }

    if (piece.family == "kick")
    {
        auto z = zone ("kick", maxLayers, cells ("Kick"), roundRobins ("kick", "kick"));
        mci::Articulation a;
        a.name = "Kick";
        a.zoneType = mci::zoneType ("kick");
        a.layers = std::move (z.audio);
        a.minVelocities = z.velocities;
        std::vector<mci::Articulation> arts;
        arts.push_back (std::move (a));
        return mci::buildInstrument (name, "Kick", arts, thumbnail(), {}, "Kick");
    }

    if (piece.family == "cymbal")
    {
        if (! has ("edge") && ! has ("bow") && ! has ("bell"))
            throw Error ((piece.name + ": a cymbal needs an edge, bow or bell recording").toStdString());
        const bool bellOnly = ! has ("edge") && ! has ("bow");   // say a BELL folder converted on its own
        const juce::String edgeKey = has ("edge") ? "edge" : has ("bow") ? "bow" : "bell";
        const juce::String bowKey = has ("bow") ? "bow" : has ("edge") ? "edge" : "bell";
        if (bellOnly)
            log ("  " + name + ": only a bell recording, using it for the edge and bow zones");
        else if (edgeKey != "edge" || bowKey != "bow")
            log ("  " + name + ": no " + (bowKey == "edge" ? "bow" : "edge") + " recording, reusing the "
                 + (bowKey == "edge" ? bowKey : edgeKey) + " for that zone");
        const bool hasBell = has ("bell") && ! bellOnly;
        const juce::String kind = hasBell ? "Ride" : "Cymbal";
        Shares available { { "edge", (int) hitsOf (edgeKey).size() }, { "bow", (int) hitsOf (bowKey).size() } };
        if (hasBell)
            available.emplace_back ("bell", (int) hitsOf ("bell").size());
        const auto shares = shareCells (cells (kind) - 1, available);   // one cell is the choke ring's

        auto edge = zone (edgeKey, maxLayers, shareOf (shares, "edge"), roundRobins ("cymbal", "edge"));
        auto bow = zone (bowKey, maxLayers, shareOf (shares, "bow"), roundRobins ("cymbal", "bow"));
        mci::Layers chokeLayers;
        if (has ("choke"))   // the Mimic keeps one choke-ring cell: the hardest recording
        {
            chokeLayers = { { readOne (hardest (hitsOf ("choke"))) } };
        }
        else if (choke)
        {
            const auto x = readOne (hardest (hitsOf (edgeKey)));
            const auto ring = mci::makeChoke (std::vector<double> (x.begin(), x.end()), sampleRate);
            const int top = (1 << 23) - 1;
            mci::Samples s (ring.size());
            for (size_t i = 0; i < ring.size(); ++i)
                s[i] = juce::jlimit (-top, top, roundHalfEven (ring[i]));
            chokeLayers = { { std::move (s) } };
        }

        std::vector<mci::Articulation> arts;
        if (hasBell)
        {
            auto bell = zone ("bell", maxLayers, shareOf (shares, "bell"), roundRobins ("cymbal", "bell"));
            arts = mci::rideArticulations (std::move (bow.audio), std::move (bell.audio), std::move (edge.audio), std::move (chokeLayers));
            arts[0].minVelocities = bow.velocities;
            arts[1].minVelocities = bell.velocities;
            arts[2].minVelocities = edge.velocities;
        }
        else
        {
            arts = mci::cymbalArticulations (std::move (edge.audio), std::move (bow.audio), std::move (chokeLayers));
            arts[0].minVelocities = edge.velocities;
            arts[1].minVelocities = bow.velocities;
        }
        return mci::buildInstrument (name, kind, arts, thumbnail(), {}, kind);
    }

    if (piece.family != "snare" && piece.family != "tom")
        throw Error ((piece.name + ": unknown instrument family " + piece.family).toStdString());
    const bool snare = piece.family == "snare";
    const juce::String kind = snare ? "Snare" : "Tom";
    struct DrumZone { const char* key; const char* zoneType; const char* label; };
    const DrumZone drumZones[] = { { "center", "drum center", "Center" }, { "side", "snare side", "Side" },
                                   { "rimclick", "drum rimclick", "Rimclick" },
                                   { "rimshot", "drum rimshot", "Rimshot" }, { "rimshotside", "snare rimshot side", "Rimshot Side" },
                                   { "sidestick", "snare sidestick", "Sidestick" } };
    auto snareOnly = [] (const juce::String& key) { return key == "side" || key == "sidestick" || key == "rimshotside"; };
    if (! sideRimshots.empty())
        log ("  " + name + ": no side rimshot recording, blending rimshot and " + sideFrom + " for it");
    juce::StringArray leftOut;
    for (const auto& [key, hits] : piece.zones)
        if (std::none_of (std::begin (drumZones), std::end (drumZones), [&] (const DrumZone& dz) { return key == dz.key && (! snareOnly (key) || snare); }))
            leftOut.addIfNotAlreadyThere (key);
    leftOut.sort (false);
    for (const auto& key : leftOut)
        log ("  " + name + ": " + key + " recordings have no " + kind.toLowerCase() + " zone on the Mimic, left out");
    Shares available;
    for (const auto& dz : drumZones)
        if (has (dz.key) && (! snareOnly (dz.key) || snare))
            available.emplace_back (dz.key, (int) hitsOf (dz.key).size());
    const auto shares = shareCells (cells (kind), available);
    std::vector<mci::Articulation> arts;
    for (const auto& dz : drumZones)
    {
        if (! has (dz.key) || (snareOnly (dz.key) && ! snare))
            continue;
        auto z = zone (dz.key, maxLayers, shareOf (shares, dz.key), roundRobins (piece.family, dz.key));
        mci::Articulation a;
        a.name = kind + " " + dz.label;
        a.zoneType = mci::zoneType (dz.zoneType);
        a.layers = std::move (z.audio);
        a.minVelocities = z.velocities;
        arts.push_back (std::move (a));
    }
    return mci::buildInstrument (name, kind, arts, thumbnail(), {}, kind, snare ? 0.599 : 0.52);
}

//==============================================================================
juce::String WavToMci::Report::toString() const
{
    juce::StringArray lines;
    if (! foundHits)
        lines.add ("No recognisable hits found.");
    else
        lines.add ("Wrote " + juce::String (written.size()) + (written.size() == 1 ? " instrument" : " instruments")
                   + (written.isEmpty() ? "." : ": " + written.joinIntoString (", ") + "."));
    if (stopped)
        lines.add ("Stopped before the end.");
    if (! failed.isEmpty())
        lines.add ("Could not convert: " + failed.joinIntoString ("; ") + ".");
    if (! skipped.isEmpty())
    {
        constexpr int shown = 10;
        juce::StringArray first;
        for (int i = 0; i < std::min (shown, skipped.size()); ++i)
            first.add ("  " + skipped[i]);
        lines.add ("Skipped " + juce::String (skipped.size()) + (skipped.size() == 1 ? " file:" : " files:"));
        lines.addArray (first);
        if (skipped.size() > shown)
            lines.add ("  ...and " + juce::String (skipped.size() - shown) + " more.");
    }
    return lines.joinIntoString ("\n");
}

juce::String WavToMci::convert (const juce::File& input, const juce::File& outDir, const Options& options,
                                std::function<void (const juce::String&)> log,
                                std::function<bool (double)> keepGoing)
{
    return run (input, outDir, options, std::move (log), std::move (keepGoing)).toString();
}

juce::String WavToMci::convert (const juce::Array<juce::File>& inputs, const juce::File& outDir, const Options& options,
                                std::function<void (const juce::String&)> log,
                                std::function<bool (double)> keepGoing)
{
    return run (inputs, outDir, options, std::move (log), std::move (keepGoing)).toString();
}

WavToMci::Report WavToMci::run (const juce::File& input, const juce::File& outDir, const Options& options,
                                std::function<void (const juce::String&)> log,
                                std::function<bool (double)> keepGoing)
{
    return run (juce::Array<juce::File> { input }, outDir, options, std::move (log), std::move (keepGoing));
}

WavToMci::Report WavToMci::run (const juce::Array<juce::File>& inputs, const juce::File& outDir, const Options& options,
                                std::function<void (const juce::String&)> log,
                                std::function<bool (double)> keepGoing)
{
    if (! log)
        log = [] (const juce::String&) {};
    if (! keepGoing)
        keepGoing = [] (double) { return true; };

    Report report;
    if (inputs.isEmpty())
    {
        report.failed.add ("nothing to convert");
        return report;
    }
    const Input src (inputs);
    if (! src.isValid())
    {
        report.skipped.addArray (src.skipped());
        for (const auto& s : report.skipped)
            log ("skipped " + s);
        const auto what = inputs.size() == 1 ? inputs.getFirst().getFullPathName() + ": not a folder or a .zip"
                        : src.skipped().size() < inputs.size() ? juce::String ("the inputs share no folder")
                                                               : juce::String ("none of the inputs is a folder");
        report.failed.add (what);
        log ("cannot read " + what);
        return report;
    }
    auto pieces = scan (src, report.skipped);
    for (const auto& s : report.skipped)
        log ("skipped " + s);
    if (pieces.empty())
    {
        log ("no recognisable hits found");
        return report;
    }
    report.foundHits = true;

    juce::Array<juce::File> parents;   // with no outDir: next to the inputs, in the folder holding them all
    for (const auto& input : inputs)
        parents.add (input.getParentDirectory());
    auto dir = outDir == juce::File() ? commonFolder (parents) : outDir;
    if (dir == juce::File())
        dir = inputs.getFirst().getParentDirectory();
    if (! dir.createDirectory())
    {
        report.failed.add ("cannot create " + dir.getFullPathName());
        return report;
    }

    const double numPieces = (double) pieces.size();
    for (size_t p = 0; p < pieces.size(); ++p)
    {
        const auto& piece = pieces[p];
        const auto info = pieceInfo (piece, src);
        const auto name = options.name.isNotEmpty() && pieces.size() == 1 ? options.name
                        : info.name.isNotEmpty() ? info.name : pieceTitle (piece, src);
        juce::StringArray counts;
        int hits = 0;
        for (const auto& [zone, zoneHits] : piece.zones)
        {
            counts.add (zone + " " + juce::String ((int) zoneHits.size()));
            hits += (int) zoneHits.size();
        }
        log (name + ": " + counts.joinIntoString (", "));

        std::vector<std::pair<juce::String, bool>> builds { { name, false } };
        if (piece.family == "hat" && options.hatAsRide)
            builds.emplace_back (name + " 3-zone", true);

        int read = 0;
        const double expected = std::max (1, hits * (int) builds.size());
        auto onWavRead = [&]
        {
            const auto within = std::min (1.0, read++ / expected);
            if (! keepGoing (((double) p + within) / numPieces))
                throw Stopped();
        };
        for (const auto& [title, asRide] : builds)
        {
            try
            {
                auto f = build (piece, src, title, options.choke, asRide, log, onWavRead, options.fullLength, options.cellsPercent);
                if (info.library.isNotEmpty())
                    f.set ("INST0libnam", mci::Value::ofString (info.library));
                if (info.image.getSize() > 0)
                    f.image = info.image;
                // The name inside keeps any quote marks; the file name can't on Windows (14" Snare).
                const auto file = dir.getChildFile (title.replaceCharacters ("/\\:", "   ").removeCharacters ("\"*?<>|")
                                                         .trim() + ".mci");
                const auto result = mci::writeMciFile (f, file);
                if (result.failed())
                    throw Error (result.getErrorMessage().toStdString());
                report.written.add (file.getFullPathName());
                log ("  wrote " + file.getFullPathName() + " (" + f.getString ("INST0instyp") + ", "
                     + juce::String (f.getInt ("INST0pool0psz")) + " samples)");
            }
            catch (const Stopped&)
            {
                report.stopped = true;
                log ("stopped");
                return report;
            }
            catch (const std::exception& e)
            {
                report.failed.add (title + ": " + e.what());
                log ("  could not convert " + title + ": " + e.what());
            }
        }
        if (! keepGoing ((double) (p + 1) / numPieces))
        {
            report.stopped = p + 1 < pieces.size();
            if (report.stopped)
                return report;
        }
    }
    return report;
}
} // namespace emk
