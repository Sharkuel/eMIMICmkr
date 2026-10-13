#include "KitMap.h"
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <iterator>

namespace emk
{
namespace
{
// Hi-hat openness steps, closed to open. wav2mci picks five of them per playing area.
const std::initializer_list<std::pair<const char*, int>> hatSteps {
    { "CLOSED", 127 }, { "OPEN1", 96 }, { "OPEN2", 64 }, { "OPEN3", 32 }, { "OPEN4", 0 },
};

void addHat (KitMap& m, int bowNote, int edgeNote, int pedalNote)
{
    for (auto [step, cc] : hatSteps)
        m.zones.add ({ "Hi-Hat", juce::String ("HATS BOW ") + step, bowNote, cc });
    for (auto [step, cc] : hatSteps)
        m.zones.add ({ "Hi-Hat", juce::String ("HATS EDGE ") + step, edgeNote, cc });
    m.zones.add ({ "Hi-Hat", "HATS PEDAL CLOSED", pedalNote, 127 });
    // Most plugins play a foot splash from the pedal note when the hat is open; off until checked.
    m.zones.add ({ "Hi-Hat", "HATS FOOT SPLASH", pedalNote, 0, false });
}
} // namespace

KitMap KitMap::generalMidi()
{
    // GM has no openness control: closed and open hats are separate notes.
    KitMap m;
    for (const auto& zone : std::initializer_list<KitZone> {
        { "Kick", "KICK1", 36 },
        { "Snare", "SNARE1 CENTER", 38 },
        { "Snare", "SNARE1 RIMSHOT", 40 },
        { "Snare", "SNARE1 SIDESTICK", 37 },
        { "Tom 1", "TOM1 CENTER", 48 },
        { "Tom 2", "TOM2 CENTER", 45 },
        { "Tom 3", "TOM3 CENTER", 43 },
        { "Hi-Hat", "HATS BOW CLOSED", 42 },
        { "Hi-Hat", "HATS BOW OPEN1", 46 },
        { "Hi-Hat", "HATS PEDAL CLOSED", 44 },
        { "Crash 1", "CRASH1 BOW", 49 },
        { "Crash 2", "CRASH2 BOW", 57 },
        { "Ride", "RIDE1 BOW", 51 },
        { "Ride", "RIDE1 BELL", 53 },
        { "Ride", "RIDE1 EDGE", 59 },
        { "China", "CHINA1 EDGE", 52 },
        { "Splash", "SPLASH1 EDGE", 55 },
         })
        m.zones.add (zone);
    return m;
}

KitMap KitMap::rolandEDrum()
{
    // Roland TD-27 default note map (support.roland.com, "TD-27 Default MIDI Note Map").
    KitMap m;
    for (const auto& zone : std::initializer_list<KitZone> {
        { "Kick", "KICK1", 36 },
        { "Snare", "SNARE1 CENTER", 38 },
        { "Snare", "SNARE1 RIMSHOT", 40 },
        { "Snare", "SNARE1 SIDESTICK", 37 },
        { "Snare", "SNARE1 RIMCLICK", 0, -1, false },
         })
        m.zones.add (zone);
    // Tom rims. Rimclick (rim only) has no Roland note: set one with Learn, then tick it.
    for (auto [tom, head, rim] : { std::tuple { 1, 48, 50 }, { 2, 45, 47 }, { 3, 43, 58 }, { 4, 41, 39 } })
    {
        const auto piece = "Tom " + juce::String (tom), label = "TOM" + juce::String (tom);
        m.zones.add ({ piece, label + " CENTER", head });
        m.zones.add ({ piece, label + " RIMSHOT", rim });
        m.zones.add ({ piece, label + " RIMCLICK", 0, -1, false });
    }
    addHat (m, 46, 26, 44);
    for (const auto& zone : std::initializer_list<KitZone> {
        { "Crash 1", "CRASH1 BOW", 49 },
        { "Crash 1", "CRASH1 EDGE", 55 },
        { "Crash 2", "CRASH2 BOW", 57 },
        { "Crash 2", "CRASH2 EDGE", 52 },
        { "Ride", "RIDE1 BOW", 51 },
        { "Ride", "RIDE1 BELL", 53 },
        { "Ride", "RIDE1 EDGE", 59 },
         })
        m.zones.add (zone);
    return m;
}

namespace
{
juce::String familyOf (const juce::String& label)
{
    return label.upToFirstOccurrenceOf (" ", false, false).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}
} // namespace

namespace
{
bool isHat (const juce::String& family)
{
    return family == "HATS" || family == "HAT" || family == "HH" || family == "HIHAT";
}

/** Wave cells the Mimic gives the instrument this zone belongs to (one cymbal cell is the choke ring). */
int cellsFor (const KitZone& zone)
{
    const auto family = familyOf (zone.label);
    const bool cymbal = family == "CRASH" || family == "SPLASH" || family == "CHINA" || family == "STACK"
                        || family == "CYMBAL";
    return family == "KICK" ? 64
         : family == "SNARE" ? 300
         : family == "TOM" ? 200
         : family == "RIDE" || cymbal ? 119   // stereo cymbals: see WavToMci::cellLimit
         : 200;
}
} // namespace

int KitMap::hitsFor (int index, int maxHits, bool fitLimits) const
{
    const auto& zone = zones.getReference (index);
    if (! fitLimits)
        return zone.hits > 0 ? juce::jmin (zone.hits, maxHits) : maxHits;
    if (isHat (familyOf (zone.label)))
        return juce::jmin (maxHits, 32, zone.hits > 0 ? zone.hits : 32);
    if (zone.hits > 0)
        return juce::jlimit (1, juce::jmin (maxHits, cellsFor (zone)), zone.hits);

    // The ticked zones without a count of their own share what the others leave.
    int taken = 0, sharing = 0;
    for (const auto& z : zones)
        if (z.enabled && z.piece == zone.piece)
        {
            if (z.hits > 0)
                taken += z.hits;
            else
                ++sharing;
        }
    return juce::jlimit (1, maxHits, (cellsFor (zone) - taken) / juce::jmax (1, sharing));
}

int KitMap::setHits (int index, int count, bool fitLimits)
{
    auto& zone = zones.getReference (index);
    if (count <= 0)
        return zone.hits = 0;
    if (! fitLimits || isHat (familyOf (zone.label)))
        return zone.hits = count;

    int othersTaken = 0;   // what the piece's other ticked zones need: their counts, or one hit each
    for (int i = 0; i < zones.size(); ++i)
        if (i != index && zones[i].enabled && zones[i].piece == zone.piece)
            othersTaken += juce::jmax (1, zones[i].hits);
    return zone.hits = juce::jlimit (1, juce::jmax (1, cellsFor (zone) - othersTaken), count);
}

std::pair<juce::String, juce::String> KitMap::guessFromName (const juce::String& noteName)
{
    const auto n = " " + noteName.toLowerCase().replaceCharacters ("-_/()[].,", "         ") + " ";
    auto has = [&n] (const char* word) { return n.contains (juce::String (" ") + word); };
    auto number = [&n] (const char* word)
    {
        const auto after = n.fromFirstOccurrenceOf (word, false, false).trimStart();
        const int v = after.getIntValue();
        return v > 0 && v < 10 ? juce::String (v) : juce::String ("1");
    };

    if (has ("kick") || has ("bass drum") || has ("bd "))
        return { "Kick", "KICK1" };
    if (has ("hat") || has ("hh"))
    {
        const auto area = has ("edge") || has ("shank") ? "EDGE" : has ("bell") ? "BELL" : "BOW";
        if (has ("splash")) return { "Hi-Hat", "HATS FOOT SPLASH" };
        if (has ("pedal") || has ("foot")) return { "Hi-Hat", "HATS PEDAL CLOSED" };
        const auto open = has ("open") ? juce::String ("OPEN") + (n.contains ("open 1") || n.contains ("open1") ? "1"
                                                               : n.contains ("open 2") || n.contains ("open2") ? "2"
                                                               : n.contains ("open 3") || n.contains ("open3") ? "3" : "4")
                        : has ("tight") ? juce::String ("TIGHT") : juce::String ("CLOSED");
        return { "Hi-Hat", juce::String ("HATS ") + area + " " + open };
    }
    if (has ("snare") || has ("sd "))
    {
        const auto zone = has ("rimclick") || has ("rim only") || has ("rimonly") ? "RIMCLICK"
                        : has ("side") || has ("cross") || has ("xstick") ? "SIDESTICK"
                        : has ("rim") && has ("shallow") ? "RIMSHOT SHALLOW"
                        : has ("rim") ? "RIMSHOT" : "CENTER";
        return { "Snare", juce::String ("SNARE1 ") + zone };
    }
    if (has ("tom") || has ("floor"))
    {
        const auto t = number (has ("floor") ? "floor" : "tom");
        const auto zone = has ("rimclick") || has ("rim only") || has ("rimonly") ? "RIMCLICK"
                        : has ("rim") ? "RIMSHOT" : "CENTER";
        const auto piece = (has ("floor") ? "Floor Tom " : "Tom ") + t;
        return { piece, (has ("floor") ? "FLOORTOM" : "TOM") + t + " " + zone };
    }
    for (auto [word, label] : { std::pair { "ride", "RIDE" }, { "crash", "CRASH" }, { "china", "CHINA" },
                                { "splash", "SPLASH" }, { "stack", "STACK" } })
        if (has (word))
        {
            const bool ride = juce::String (label) == "RIDE";
            const auto zone = has ("bell") || has ("cup") ? "BELL"
                            : has ("edge") || (ride && has ("crash")) ? "EDGE"   // a ride's "crash" is its edge
                            : has ("choke") || has ("grab") ? "CHOKE"
                            : has ("bow") || has ("tip") || ride ? "BOW"
                            : "EDGE";
            const auto t = number (word);
            return { juce::String (label).substring (0, 1) + juce::String (label).substring (1).toLowerCase() + " " + t,
                     juce::String (label) + t + " " + zone };
        }
    return {};
}

namespace
{
// Commas split cells and double quotes would open a quoted cell: 14" is written 14''.
juce::String csvText (const juce::String& s) { return s.replace (",", " ").replace ("\"", "''"); }
juce::String fromCsvText (const juce::String& s) { return s.replace ("''", "\""); }
} // namespace

juce::String KitMap::toCsv() const
{
    juce::String out = "piece,zone,note,cc4,record,hits,instrument,name,library,image\n";
    for (const auto& z : zones)
        out << z.piece.replace (",", " ") << "," << z.label.replace (",", " ") << "," << z.note << ","
            << (z.cc4 >= 0 ? juce::String (z.cc4) : juce::String()) << "," << (z.enabled ? "yes" : "no") << ","
            << (z.hits > 0 ? juce::String (z.hits) : juce::String()) << "," << z.instrument.replace (",", " ") << ","
            << csvText (z.name) << "," << csvText (z.library) << ","
            << (z.image.getSize() > 0 ? z.image.toBase64Encoding() : juce::String()) << "\n";
    return out;
}

KitMap KitMap::fromCsv (const juce::String& text, juce::String& error)
{
    KitMap m;
    int lineNumber = 0;
    for (auto line : juce::StringArray::fromLines (text))
    {
        ++lineNumber;
        line = line.trim();
        if (line.isEmpty() || line.startsWithIgnoreCase ("piece,") || line.startsWithChar ('#'))
            continue;
        auto cells = juce::StringArray::fromTokens (line, ",", "\"");
        cells.trim();
        if (cells.size() < 3 || ! cells[2].containsOnly ("0123456789") || cells[2].getIntValue() > 127)
        {
            error = "Line " + juce::String (lineNumber) + " should read piece,zone,note[,cc4[,record[,hits]]]: " + line;
            return {};
        }
        const bool record = cells.size() < 5 || ! cells[4].equalsIgnoreCase ("no");
        m.zones.add ({ cells[0], cells[1].toUpperCase(), cells[2].getIntValue(),
                       cells[3].isNotEmpty() ? juce::jlimit (0, 127, cells[3].getIntValue()) : -1, record });
        m.zones.getReference (m.zones.size() - 1).hits = juce::jmax (0, cells[5].getIntValue());
        auto& z = m.zones.getReference (m.zones.size() - 1);
        z.instrument = cells[6];
        z.name = fromCsvText (cells[7]);
        z.library = fromCsvText (cells[8]);
        if (cells[9].isNotEmpty())
            z.image.fromBase64Encoding (cells[9]);
    }
    if (m.zones.isEmpty())
        error = "No zones found.";
    return m;
}

void KitMap::applyNames (const std::map<juce::String, DrumInfo>& drums,
                         const std::map<juce::String, juce::String>& libraries)
{
    juce::StringArray taken;
    std::map<juce::String, juce::String> named;   // piece -> name given
    for (auto& z : zones)
    {
        if (z.instrument.isEmpty())
            continue;
        if (auto lib = libraries.find (z.libraryId()); lib != libraries.end() && lib->second.isNotEmpty())
            z.library = lib->second;
        const auto drum = drums.find (z.instrument);
        if (drum == drums.end())
            continue;
        if (drum->second.image.getSize() > 0)
            z.image = drum->second.image;
        if (drum->second.name.isEmpty())
            continue;
        if (named.count (z.piece) == 0)
        {
            auto name = drum->second.name;
            for (int n = 2; taken.contains (name); ++n)
                name = drum->second.name + " " + juce::String (n);
            taken.add (name);
            named[z.piece] = name;
        }
        z.name = named[z.piece];
    }
}

bool KitMap::sharesPiece (const juce::String& piece)
{
    return piece.isNotEmpty() && piece != "?";   // "?" rows are notes still to sort out, not one piece
}

void KitMap::movePiece (int index, const juce::String& piece)
{
    // A row moved into another piece takes on that piece's drum; into a new one, it starts afresh.
    auto& zone = zones.getReference (index);
    zone.piece = piece;
    for (int i = 0; i < zones.size(); ++i)
        if (i != index && sharesPiece (piece) && zones[i].piece == piece)
        {
            zone.name = zones[i].name;
            zone.library = zones[i].library;
            zone.image = zones[i].image;
            zone.instrument = zones[i].instrument;
            return;
        }
    zone.name = {};
    zone.image = {};
    zone.instrument = {};
}

juce::String KitMap::nameFromFileName (const juce::String& fileName)
{
    const auto stem = fileName.upToLastOccurrenceOf (".", false, false).isNotEmpty()
                          ? fileName.upToLastOccurrenceOf (".", false, false) : fileName;
    // Spaces for underscores, and between words run together: "YamahaVintage", "DWTimeless".
    juce::String name;
    for (int i = 0; i < stem.length(); ++i)
    {
        const auto c = stem[i];
        const auto prev = i > 0 ? stem[i - 1] : 0, next = i + 1 < stem.length() ? stem[i + 1] : 0;
        const bool upper = juce::CharacterFunctions::isUpperCase (c);
        if (upper && i > 0 && (juce::CharacterFunctions::isLowerCase (prev)
                               || (juce::CharacterFunctions::isUpperCase (prev) && juce::CharacterFunctions::isLowerCase (next))))
            name << ' ';
        name << (c == '_' ? juce::juce_wchar (' ') : c);
    }
    while (name.contains ("  "))
        name = name.replace ("  ", " ");
    name = name.trim();
    const auto lower = name.toLowerCase();
    for (const char* generic : { "image", "img", "screenshot", "unnamed", "pasted", "untitled" })
        if (lower.startsWith (generic))
            return {};
    return name.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") ? name : juce::String();
}

void KitMap::setPieceName (int index, const juce::String& name)
{
    const auto piece = zones[index].piece;
    for (int i = 0; i < zones.size(); ++i)
        if (i == index || (sharesPiece (piece) && zones[i].piece == piece))
            zones.getReference (i).name = name;
}

void KitMap::setPieceLibrary (int index, const juce::String& library)
{
    const auto piece = zones[index].piece;
    for (int i = 0; i < zones.size(); ++i)
        if (i == index || (sharesPiece (piece) && zones[i].piece == piece))
            zones.getReference (i).library = library;
}

void KitMap::setPieceImage (int index, const juce::MemoryBlock& image)
{
    const auto piece = zones[index].piece;
    for (int i = 0; i < zones.size(); ++i)
        if (i == index || (sharesPiece (piece) && zones[i].piece == piece))
            zones.getReference (i).image = image;
}

namespace
{
/** Runs find over a plugin state or preset file and, when it finds nothing there, over what the
    file may wrap: a VST3 state as JUCE hosts hand it over ("VC2!", a size, then XML holding the
    plugin's own data in base64 under IComponent), or gzip. */
juce::String findInState (const juce::MemoryBlock& data, const std::function<juce::String (const char*, size_t)>& find)
{
    if (auto text = find (static_cast<const char*> (data.getData()), data.getSize()); text.isNotEmpty())
        return text;
    if (data.getSize() > 8 && std::memcmp (data.getData(), "VC2!", 4) == 0)
    {
        const auto* bytes = static_cast<const char*> (data.getData());
        const auto size = (size_t) juce::ByteOrder::littleEndianInt (bytes + 4);
        if (size <= data.getSize() - 8)
            if (auto xml = juce::parseXML (juce::String::fromUTF8 (bytes + 8, (int) size)))
                if (auto* component = xml->getChildByName ("IComponent"))
                {
                    juce::MemoryBlock inner;
                    if (inner.fromBase64Encoding (component->getAllSubText().trim()))
                        if (auto text = findInState (inner, find); text.isNotEmpty())
                            return text;
                }
    }
    // Some hosts and plugins compress their state.
    juce::MemoryInputStream raw (data, false);
    juce::GZIPDecompressorInputStream unzipped (raw);
    juce::MemoryBlock inflated;
    juce::MemoryOutputStream out (inflated, false);
    out.writeFromInputStream (unzipped, 64 * 1024 * 1024);
    out.flush();
    return find (static_cast<const char*> (inflated.getData()), inflated.getSize());
}
} // namespace

juce::String KitMap::sd3TextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        static const std::string marker = "PlugVersion";
        const auto at = std::search (bytes, bytes + size, marker.begin(), marker.end());
        if (at == bytes + size)
            return {};
        const auto end = std::find (at, bytes + size, '\0');
        return juce::String::fromUTF8 (at, (int) (end - at));
    });
}

juce::String KitMap::bfd3TextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        // BFD3 keeps its kit, mixer and MIDI map as one XML document; its kit presets (.bfd3kit)
        // are the kit part of it.
        static const std::string open = "<root", close = "</root>", slot = "kit_slot_";
        const auto at = std::search (bytes, bytes + size, open.begin(), open.end());
        if (at == bytes + size)
            return {};
        auto end = std::search (at, bytes + size, close.begin(), close.end());
        end = end == bytes + size ? end : end + close.size();
        if (std::search (at, end, slot.begin(), slot.end()) == end)
            return {};
        return juce::String::fromUTF8 (at, (int) (end - at));
    });
}

KitMap KitMap::fromSd3 (const juce::String& text, juce::String& error)
{
    struct Slot
    {
        juce::String pos, instrument;
        std::vector<std::pair<juce::String, juce::Array<int>>> pads;   // in the order SD3 lists them
    };
    std::vector<Slot> slots;
    juce::String library;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.startsWith ("xpad "))   // xpad 0 SL-DRUMFACTORYARCADES {
            library = juce::StringArray::fromTokens (line, " ", "")[2];
        else if (line.startsWith ("pos \""))
            slots.push_back ({ line.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false), {}, {} });
        else if (line.startsWith ("drum \"") && ! slots.empty())
            slots.back().instrument = library + "/" + line.fromFirstOccurrenceOf ("\"", false, false)
                                                            .upToFirstOccurrenceOf ("\"", false, false);
        else if (line.startsWith ("alias ") && ! slots.empty())
        {
            auto words = juce::StringArray::fromTokens (line, " ", "");
            words.removeEmptyStrings();
            juce::Array<int> notes;
            for (int w = 2; w < words.size(); ++w)
                if (words[w].containsOnly ("0123456789") && words[w].getIntValue() <= 127)
                    notes.add (words[w].getIntValue());
            if (words.size() > 1 && ! notes.isEmpty())
                slots.back().pads.push_back ({ words[1], notes });
        }
    }
    if (slots.empty())
    {
        error = "This isn't a Superior Drummer 3 kit (no instrument slots found).";
        return {};
    }

    KitMap m;
    std::map<juce::String, int> counts;   // the how-manyth tom, crash... so far
    for (const auto& slot : slots)
    {
        const int firstZone = m.zones.size();
        struct TagZones   // every zone added for this slot carries the drum's ID
        {
            KitMap& map; int from; juce::String id;
            ~TagZones() { for (int z = from; z < map.zones.size(); ++z) map.zones.getReference (z).instrument = id; }
        } tag { m, firstZone, slot.instrument };
        const auto pos = slot.pos.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz");
        const auto family = pos.startsWith ("kick") ? "kick"
                          : pos.startsWith ("hat") ? "hats"
                          : pos.contains ("snare") ? "snare"
                          : pos.contains ("tom") ? "tom"
                          : pos.startsWith ("ride") ? "ride"
                          : pos.startsWith ("crash") ? "crash"
                          : pos.startsWith ("splash") ? "splash"
                          : pos.startsWith ("china") ? "china" : "";
        if (juce::String (family).isEmpty())
        {
            // Octobans, cowbells, chimes...: a Mimic percussion instrument, whose four zones take
            // the slot's first four pads. Main is ticked; listen to the others before ticking them.
            static const char* percZones[] = { "MAIN", "SECONDARY", "ADD1", "ADD2" };
            const auto head = "PERC " + slot.pos.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
            int zone = 0;
            for (const auto& [pad, notes] : slot.pads)
            {
                if (pad.contains ("Trig") || pad.contains ("Ctrl"))
                    continue;
                const auto label = zone < 4 ? head + " " + percZones[zone] : "NOTE " + juce::String (notes[0]);
                m.zones.add ({ zone < 4 ? slot.pos : juce::String ("?"), label, notes[0], -1, zone == 0, slot.pos + ": " + pad });
                ++zone;
            }
            continue;
        }
        const int n = ++counts[family];
        const juce::String num (n), upper = juce::String (family).toUpperCase();
        const juce::String piece = juce::String (family) == "kick" ? (n == 1 ? "Kick" : "Kick " + num)
                                 : juce::String (family) == "hats" ? "Hi-Hat"
                                 : juce::String (family) == "snare" ? (n == 1 ? "Snare" : "Snare " + num)
                                 : juce::String (family).substring (0, 1).toUpperCase() + juce::String (family).substring (1) + " " + num;

        auto noteOf = [&slot] (const juce::String& suffix) -> int
        {
            for (const auto& [pad, notes] : slot.pads)
                if (pad.trimCharactersAtStart ("abcdefghijklmnopqrstuvwxyz0123456789") == suffix)
                    return notes[0];
            return -1;
        };

        if (juce::String (family) == "hats")
        {
            // SD3's trigger notes follow the pedal (CC4), as an e-kit's hi-hat does.
            const int tip = noteOf ("TipTrig"), edge = noteOf ("Trig"), pedal = noteOf ("PL");
            if (tip >= 0 || edge >= 0)
            {
                const int before = m.zones.size();
                addHat (m, tip >= 0 ? tip : edge, edge >= 0 ? edge : tip, pedal >= 0 ? pedal : 44);
                for (int z = before; z < m.zones.size(); ++z)
                    m.zones.getReference (z).pluginName = slot.pos;
                if (const int splash = noteOf ("PLO"); splash >= 0)
                    for (int z = before; z < m.zones.size(); ++z)
                        if (m.zones[z].label == "HATS FOOT SPLASH")
                            m.zones.getReference (z).note = splash;
            }
            continue;
        }

        // Articulation codes as SD3 names its pads: <slot><CODE>, e.g. snareRO, ride4BL.
        struct Code { const char* code; const char* zone; bool on; };
        static const std::map<juce::String, std::vector<Code>> codes {
            { "kick", { { "R", "", true }, { "O", "OPEN", false } } },
            { "snare", { { "R", "CENTER", true }, { "RO", "RIMSHOT", true }, { "SL", "SIDESTICK", true },
                         { "FX", "RIMCLICK", false }, { "FO", "EDGE", false }, { "MC", "OFF CENTER", false },
                         { "MR", "RIMSHOT EDGE", false }, { "BZ", "BUZZ", false }, { "FL", "FLAM", false } } },
            { "tom", { { "R", "CENTER", true }, { "RO", "RIMSHOT", true }, { "FX", "RIMCLICK", false } } },
            { "ride", { { "", "BOW", true }, { "BL", "BELL", true }, { "PU", "EDGE", true }, { "M", "CHOKE", false } } },
            { "crash", { { "", "EDGE", true }, { "RT", "BOW", true }, { "BL", "BELL", false }, { "M", "CHOKE", false } } },
        };
        const auto& table = codes.count (family) ? codes.at (family) : codes.at ("crash");   // splash, china
        const juce::String labelHead = upper + num;
        std::set<juce::String> used;
        for (const auto& c : table)
            if (const int note = noteOf (c.code); note >= 0)
            {
                m.zones.add ({ piece, juce::String (c.zone).isEmpty() ? labelHead : labelHead + " " + c.zone, note, -1, c.on,
                               slot.pos + ": " + juce::String (c.code).toLowerCase() });
                used.insert (c.code);
            }
        for (const auto& [pad, notes] : slot.pads)   // anything else: there to listen to and tick
        {
            const auto code = pad.trimCharactersAtStart ("abcdefghijklmnopqrstuvwxyz0123456789");
            if (used.count (code) == 0 && ! code.contains ("Trig") && ! code.contains ("Ctrl"))
                m.zones.add ({ piece, labelHead + " " + (code.isEmpty() ? pad : code).toUpperCase(), notes[0], -1, false,
                               slot.pos + ": " + (code.isEmpty() ? pad : code) });
        }
    }
    return m;
}

namespace
{
// BFD3's own "BFD 3.0" MIDI map as { note, slot, articulation }, for kit presets, which leave
// the map out. A plugin state carries the map actually in use.
const int bfd3DefaultMap[][3] {
    { 18, 2, 11 }, { 20, 2, 12 }, { 22, 2, 20 }, { 24, 0, 0 }, { 25, 1, 4 }, { 26, 1, 0 }, { 27, 1, 1 }, { 28, 1, 5 },
    { 29, 1, 3 }, { 30, 2, 8 }, { 31, 3, 0 }, { 32, 2, 10 }, { 33, 12, 0 }, { 34, 2, 0 }, { 35, 4, 0 }, { 36, 13, 0 },
    { 37, 2, 9 }, { 38, 5, 0 }, { 39, 2, 1 }, { 40, 14, 0 }, { 41, 6, 2 }, { 42, 2, 6 }, { 43, 15, 2 }, { 44, 2, 7 },
    { 45, 7, 2 }, { 46, 2, 4 }, { 47, 16, 2 }, { 48, 50, 2 }, { 49, 2, 5 }, { 50, 8, 2 }, { 51, 2, 2 }, { 52, 6, 3 },
    { 53, 15, 3 }, { 54, 2, 3 }, { 55, 7, 3 }, { 56, 2, 13 }, { 57, 16, 3 }, { 58, 2, 14 }, { 59, 50, 3 }, { 60, 8, 3 },
    { 61, 3, 2 }, { 62, 6, 1 }, { 63, 12, 2 }, { 64, 15, 1 }, { 65, 7, 1 }, { 66, 4, 2 }, { 67, 16, 1 }, { 68, 13, 2 },
    { 69, 50, 1 }, { 70, 5, 2 }, { 71, 8, 1 }, { 72, 6, 0 }, { 73, 14, 2 }, { 74, 15, 0 }, { 75, 6, 8000 }, { 76, 7, 0 },
    { 77, 16, 0 }, { 78, 15, 8000 }, { 79, 50, 0 }, { 80, 7, 8000 }, { 81, 8, 0 }, { 82, 16, 8000 }, { 83, 9, 0 },
    { 84, 0, 1 }, { 85, 50, 8000 }, { 86, 1, 2 }, { 87, 8, 8000 }, { 88, 1, 7 }, { 89, 3, 3 }, { 90, 12, 3 },
    { 91, 4, 3 }, { 92, 13, 3 }, { 93, 5, 3 }, { 94, 14, 3 }, { 95, 18, 0 },
};
constexpr int bfd3Choke = 8000;   // the articulation BFD3's map uses for grabbing a cymbal

void forEachElement (const juce::XmlElement& e, const std::function<void (const juce::XmlElement&)>& f)
{
    f (e);
    for (auto* child : e.getChildIterator())
        forEachElement (*child, f);
}
} // namespace

juce::MemoryBlock KitMap::bfd3WithEveryDrumMapped (const juce::MemoryBlock& state, int& mapped)
{
    mapped = 0;
    // A VST3 state as JUCE hosts hand it over: "VC2!", a size, then XML holding the plugin's
    // own data in base64 under IComponent. BFD3's own data is "BFDstate", a header, then XML.
    const auto* bytes = static_cast<const char*> (state.getData());
    if (state.getSize() < 8 || std::memcmp (bytes, "VC2!", 4) != 0)
        return {};
    const auto outerSize = (size_t) juce::ByteOrder::littleEndianInt (bytes + 4);
    if (outerSize > state.getSize() - 8)
        return {};
    const std::string outer (bytes + 8, outerSize);
    const auto b64Start = outer.find ("<IComponent>"), b64End = outer.find ("</IComponent>");
    if (b64Start == std::string::npos || b64End == std::string::npos || b64End < b64Start)
        return {};
    juce::MemoryBlock innerBlock;
    if (! innerBlock.fromBase64Encoding (juce::String (outer.substr (b64Start + 12, b64End - b64Start - 12)).trim()))
        return {};
    std::string inner (static_cast<const char*> (innerBlock.getData()), innerBlock.getSize());
    const auto rootStart = inner.find ("<root"), rootEnd = inner.find ("</root>");
    if (inner.compare (0, 8, "BFDstate") != 0 || rootStart == std::string::npos || rootEnd == std::string::npos)
        return {};

    const auto xml = juce::parseXML (juce::String::fromUTF8 (inner.data() + rootStart, (int) (rootEnd + 7 - rootStart)));
    if (xml == nullptr)
        return {};
    std::map<int, juce::String> items;
    std::map<int, std::vector<int>> artics;   // slot -> its loaded articulations
    std::set<int> mappedSlots;
    std::vector<int> freeNotes, grooveNotes;
    forEachElement (*xml, [&] (const juce::XmlElement& e)
    {
        const auto name = e.getStringAttribute ("name");
        if (e.hasTagName ("VString") && name.startsWith ("kit_slot_") && e.getAllSubText().trim().isNotEmpty())
            items[name.getTrailingIntValue()] = e.getAllSubText().trim();
        else if (e.hasTagName ("BFD2KitPiece") && name.startsWith ("kp_slot_"))
            forEachElement (e, [&] (const juce::XmlElement& a)
            {
                if (a.hasTagName ("BFD2Artic") && a.getStringAttribute ("name").startsWith ("artic_"))
                    artics[name.getTrailingIntValue()].push_back (a.getStringAttribute ("name").fromFirstOccurrenceOf ("_", false, false).getIntValue());
            });
        else if (e.hasTagName ("VObjectArray") && name.startsWith ("midi_note_"))
        {
            const auto note = name.getTrailingIntValue();
            const auto* element = e.getChildByName ("MIDIMapElement");
            const auto* req = element != nullptr ? element->getChildByName ("RequestedArticulationID") : nullptr;
            if (req != nullptr)
                mappedSlots.insert (req->getIntAttribute ("slot"));
            else if (element != nullptr && element->getBoolAttribute ("isgrv"))
                grooveNotes.push_back (note);
            else
                freeNotes.push_back (note);
        }
    });
    freeNotes.insert (freeNotes.end(), grooveNotes.begin(), grooveNotes.end());

    std::vector<std::pair<int, int>> wanted;   // (slot, articulation) without a note
    for (const auto& [slot, item] : items)
        if (slot >= 0 && mappedSlots.count (slot) == 0)
            for (const int artic : artics[slot])
                wanted.emplace_back (slot, artic);
    if (wanted.empty() || freeNotes.empty())
        return {};

    for (size_t k = 0; k < wanted.size() && k < freeNotes.size(); ++k)
    {
        const auto note = std::to_string (freeNotes[k]);
        const auto open = "<VObjectArray name=\"midi_note_" + note + "\"";
        const auto at = inner.find (open, rootStart);
        if (at == std::string::npos || at > inner.find ("</root>"))
            continue;
        // The element is either self-closing or runs to its </VObjectArray>.
        const auto tagEnd = inner.find ('>', at);
        const auto end = inner[tagEnd - 1] == '/' ? tagEnd + 1 : inner.find ("</VObjectArray>", tagEnd) + 15;
        const auto replacement = open + ">\r\n"
            "            <MIDIMapElement note=\"" + note + "\" isgrv=\"false\" minCC=\"0\" maxCC=\"1\" curve=\"0\" minInCC=\"0\" maxInCC=\"1\" inGate=\"false\">\r\n"
            "                <RequestedArticulationID name=\"req_artic\" slot=\"" + std::to_string (wanted[k].first)
            + "\" artic=\"" + std::to_string (wanted[k].second) + "\"/>\r\n"
            "            </MIDIMapElement>\r\n"
            "        </VObjectArray>";
        inner.replace (at, end - at, replacement);
        ++mapped;
    }
    if (mapped == 0)
        return {};

    juce::MemoryBlock newInner (inner.data(), inner.size());
    const auto newOuter = outer.substr (0, b64Start + 12) + newInner.toBase64Encoding().toStdString() + outer.substr (b64End);
    juce::MemoryBlock out;
    out.append ("VC2!", 4);
    const auto size = juce::ByteOrder::swapIfBigEndian ((juce::uint32) newOuter.size());
    out.append (&size, 4);
    out.append (newOuter.data(), newOuter.size());
    out.append (bytes + 8 + outerSize, state.getSize() - 8 - outerSize);   // whatever followed, as it was
    return out;
}

KitMap KitMap::fromBfd3 (const juce::String& text, juce::String& error)
{
    auto xml = juce::parseXML (text);
    std::map<int, juce::String> items;                     // slot -> "Crush Drums/Kick No Reso Slam Pad"
    std::map<int, std::map<int, juce::String>> artics;     // slot -> articulation -> "Rim Click"
    std::map<std::pair<int, int>, int> notes;              // (slot, articulation) -> lowest note
    juce::String kitName;
    if (xml != nullptr)
        forEachElement (*xml, [&] (const juce::XmlElement& e)
        {
            const auto name = e.getStringAttribute ("name");
            if (e.hasTagName ("VString") && name.startsWith ("kit_slot_"))
                items[name.getTrailingIntValue()] = e.getAllSubText().trim();
            else if (e.hasTagName ("BFD2KitInfo") && ! e.getStringAttribute ("info_name").startsWith ("temp_"))
                kitName = e.getStringAttribute ("info_name");
            else if (e.hasTagName ("BFD2KitPiece") && name.startsWith ("kp_slot_"))
                forEachElement (e, [&] (const juce::XmlElement& a)
                {
                    if (a.hasTagName ("BFD2Artic") && a.getStringAttribute ("name").startsWith ("artic_"))
                        artics[name.getTrailingIntValue()][a.getStringAttribute ("name").fromFirstOccurrenceOf ("_", false, false).getIntValue()]
                            = a.getStringAttribute ("artic_name");
                });
            else if (e.hasTagName ("MIDIMapElement"))
                if (auto* req = e.getChildByName ("RequestedArticulationID"))
                    notes.insert ({ { req->getIntAttribute ("slot"), req->getIntAttribute ("artic") }, e.getIntAttribute ("note") });
        });
    items.erase (-1);
    for (auto it = items.begin(); it != items.end();)
        it = it->second.isEmpty() ? items.erase (it) : std::next (it);
    if (items.empty())
    {
        error = xml == nullptr ? "This isn't a BFD3 kit." : "The BFD3 kit is empty: load drums into it first.";
        return {};
    }
    if (notes.empty())
        for (const auto& [note, slot, artic] : bfd3DefaultMap)
            notes.insert ({ { slot, artic }, note });

    // What each slot holds, from the drum's name, or else from where BFD3's default kit puts it.
    auto familyOf = [] (int slot, const juce::String& item)
    {
        const auto n = item.fromLastOccurrenceOf ("/", false, false).toLowerCase();
        if (n.contains ("kick")) return "kick";
        if (n.contains ("snare")) return "snare";
        if (n.contains ("hihat") || n.contains ("hi-hat") || n.contains ("hat")) return "hats";
        if (n.contains ("tom")) return "tom";
        if (n.contains ("ride")) return "ride";
        if (n.contains ("splash")) return "splash";
        if (n.contains ("china")) return "china";
        if (n.contains ("crash") || n.contains ("cym")) return "crash";   // "Cymbal", 8BK's "Cym 1"
        switch (slot)
        {
            case 0: return "kick";
            case 1: return "snare";
            case 2: return "hats";
            case 3: case 4: case 5: case 12: case 13: case 14: return "tom";
            case 8: return "ride";
            case 6: case 7: case 15: case 16: case 47: case 50: return "crash";
            default: return "";
        }
    };
    // Kick, snare and hi-hat, then the toms high to low as BFD3's default kit places them (High Tom 1
    // and 2, Mid Tom 1 and 2, Floor Tom 1 and 2), then the rest.
    auto order = [] (int slot)
    {
        static const int toms[] { 14, 5, 13, 4, 3, 12 };
        const auto at = std::find (std::begin (toms), std::end (toms), slot);
        return at != std::end (toms) ? 10 + (int) (at - std::begin (toms)) : slot <= 2 ? slot : 100 + slot;
    };
    std::vector<int> slots;
    for (const auto& item : items)
        slots.push_back (item.first);
    std::stable_sort (slots.begin(), slots.end(), [&] (int a, int b) { return order (a) < order (b); });

    KitMap m;
    m.kitName = kitName;
    std::map<juce::String, int> counts;
    juce::StringArray names;
    for (const int slot : slots)
    {
        const auto& item = items[slot];
        const juce::String family = familyOf (slot, item);
        const auto drum = item.fromLastOccurrenceOf ("/", false, false);
        auto name = drum;
        for (int n = 2; names.contains (name); ++n)
            name = drum + " " + juce::String (n);
        names.add (name);
        const int firstZone = m.zones.size();
        auto add = [&] (const juce::String& piece, const juce::String& label, int artic, bool on)
        {
            if (auto it = notes.find ({ slot, artic }); it != notes.end())
                m.zones.add ({ piece, label, it->second, -1, on,
                               drum + ": " + (artic == bfd3Choke ? juce::String ("Choke") : artics[slot][artic]) });
        };

        if (family.isEmpty())
        {
            // Percussion: a Mimic percussion instrument takes the first four articulations.
            static const char* percZones[] = { "MAIN", "SECONDARY", "ADD1", "ADD2" };
            const auto head = "PERC " + drum.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
            int zone = 0;
            for (const auto& [artic, artName] : artics[slot])
                if (zone < 4)
                {
                    add (drum, head + " " + percZones[zone], artic, zone == 0);
                    ++zone;
                }
        }
        else if (family == "hats")
        {
            // BFD3 plays each openness from its own note: Tip (bow) and Shank (edge), Closed to Open.
            static const std::pair<const char*, const char*> steps[] {
                { "Closed", "CLOSED" }, { "Quarter", "OPEN1" }, { "Half", "OPEN2" }, { "Threeq", "OPEN3" }, { "Open", "OPEN4" },
            };
            for (const auto& [area, label] : { std::pair { "T", "BOW" }, std::pair { "S", "EDGE" } })
                for (const auto& [bfd, step] : steps)
                    for (const auto& [artic, artName] : artics[slot])
                        if (artName == juce::String (bfd) + area)
                            add ("Hi-Hat", juce::String ("HATS ") + label + " " + step, artic, true);
            for (const auto& [artic, artName] : artics[slot])
                if (artName == "Pedal")
                    add ("Hi-Hat", "HATS PEDAL CLOSED", artic, true);
                else if (artName == "Splash")
                    add ("Hi-Hat", "HATS FOOT SPLASH", artic, true);
        }
        else
        {
            const int n = ++counts[family];
            const juce::String num (n);
            const juce::String piece = family == "kick" ? (n == 1 ? juce::String ("Kick") : "Kick " + num)
                                     : family == "snare" ? (n == 1 ? juce::String ("Snare") : "Snare " + num)
                                     : family.substring (0, 1).toUpperCase() + family.substring (1) + " " + num;
            const auto head = family.toUpperCase() + num;
            // BFD3's articulation names -> the Mimic's zones, and which are ticked to start with.
            struct Zone { const char* bfd; const char* zone; bool on; };
            static const std::map<juce::String, std::vector<Zone>> zonesOf {
                { "kick", { { "Hit", "", true } } },
                { "snare", { { "Hit", "CENTER", true }, { "Rim", "RIMSHOT", true }, { "SS", "SIDESTICK", true },
                             { "Rim Click", "RIMCLICK", false }, { "HalfEdge", "EDGE", false } } },
                { "tom", { { "Hit", "CENTER", true }, { "Rim", "RIMSHOT", true }, { "Rim Click", "RIMCLICK", true } } },
                { "ride", { { "Bow", "BOW", true }, { "Bell", "BELL", true }, { "Edge", "EDGE", true } } },
                { "crash", { { "Edge", "EDGE", true }, { "Bow", "BOW", true }, { "Bell", "BELL", false } } },
            };
            const auto& table = zonesOf.count (family) ? zonesOf.at (family) : zonesOf.at ("crash");
            std::set<int> used;
            for (const auto& z : table)
                for (const auto& [artic, artName] : artics[slot])
                    if (artName == z.bfd)
                    {
                        add (piece, juce::String (z.zone).isEmpty() ? head : head + " " + z.zone, artic, z.on);
                        used.insert (artic);
                    }
            // Some cymbals have a plain "Hit" instead: the bow of a ride, the edge of anything else.
            const bool cymbal = family != "kick" && family != "snare" && family != "tom";
            const juce::String hitZone = family == "ride" ? "BOW" : "EDGE";
            const auto has = [&] (const juce::String& artName)
            {
                return std::any_of (artics[slot].begin(), artics[slot].end(), [&] (const auto& a) { return a.second == artName; });
            };
            if (cymbal && ! has (family == "ride" ? "Bow" : "Edge"))
                for (const auto& [artic, artName] : artics[slot])
                    if (artName == "Hit")
                    {
                        add (piece, head + " " + hitZone, artic, true);
                        used.insert (artic);
                    }
            for (const auto& [artic, artName] : artics[slot])   // anything else: there to listen to and tick
                if (used.count (artic) == 0)
                    add (piece, head + " " + artName.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "), artic, false);
            if (cymbal)
                add (piece, head + " CHOKE", bfd3Choke, false);
        }
        for (int z = firstZone; z < m.zones.size(); ++z)
        {
            auto& zone = m.zones.getReference (z);
            zone.instrument = item;
            zone.name = name;
            zone.library = item.upToFirstOccurrenceOf ("/", false, false);
        }
    }
    return m;
}

namespace
{
/** MODO DRUM keeps its whole session as a JUCE ValueTree, in binary, starting at SESSION_STATE. */
juce::ValueTree modoTreeIn (const juce::MemoryBlock& data)
{
    juce::ValueTree tree;
    findInState (data, [&tree] (const char* bytes, size_t size) -> juce::String
    {
        static const std::string marker ("SESSION_STATE\0", 14);
        const auto at = std::search (bytes, bytes + size, marker.begin(), marker.end());
        if (at == bytes + size)
            return {};
        tree = juce::ValueTree::readFromData (at, (size_t) (bytes + size - at));
        return tree.isValid() ? "found" : "";
    });
    return tree;
}

/** "Crash16_Custom" -> "Crash 16 Custom", "FloorTom0_Metal" -> "Floor Tom 0 Metal". */
juce::String modoName (const juce::String& id)
{
    juce::String name;
    for (int i = 0; i < id.length(); ++i)
    {
        const juce::juce_wchar c = id[i], before = i > 0 ? id[i - 1] : ' ';
        if (c == '_')
        {
            name << ' ';
            continue;
        }
        const bool newWord = before != ' ' && before != '_'
                             && ((juce::CharacterFunctions::isUpperCase (c) && juce::CharacterFunctions::isLowerCase (before))
                                 || (juce::CharacterFunctions::isDigit (c) != juce::CharacterFunctions::isDigit (before)));
        if (newWord)
            name << ' ';
        name << juce::String::charToString (c);
    }
    return name;
}
} // namespace

bool KitMap::isModoState (const juce::MemoryBlock& data)
{
    return modoTreeIn (data).getChildWithName ("MIDI_MAP").isValid();
}

KitMap KitMap::fromModo (const juce::MemoryBlock& state, juce::String& error)
{
    const auto tree = modoTreeIn (state);
    const auto midiMap = tree.getChildWithName ("MIDI_MAP");
    if (! midiMap.isValid())
    {
        error = "This isn't a MODO DRUM state.";
        return {};
    }

    // Which instrument each slot holds, and whether it's in the kit at all.
    std::map<juce::String, juce::String> elements;   // "Slot2Cymbal" -> "Crash16_Custom"
    for (const auto& slot : tree.getChildWithName ("PROJECT"))
        if (const auto element = slot.getChildWithName ("ELEMENT"); element.isValid() && slot.getProperty ("isEnabled", true))
            elements[slot.getType().toString()] = element["referencedElementId"].toString();

    auto familyOf = [] (const juce::String& slot, const juce::String& element)
    {
        const auto s = slot.toLowerCase(), e = element.toLowerCase();
        if (s.contains ("kick")) return "kick";
        if (s.contains ("snare")) return "snare";
        if (s.contains ("hihat")) return "hats";
        if (s.contains ("tom")) return "tom";
        if (s.contains ("ride")) return "ride";
        if (s.contains ("cymbal"))
            return e.startsWith ("china") ? "china" : e.startsWith ("splash") ? "splash" : "crash";
        return "";
    };
    // Kicks (right, then left), snare, hi-hat, toms high to low, cymbals, ride, then percussion.
    auto order = [&familyOf] (const juce::String& slot, const juce::String& element)
    {
        static const juce::StringArray families { "kick", "snare", "hats", "tom", "crash", "splash", "china", "ride", "" };
        static const juce::StringArray toms { "Tom0", "Tom1", "Tom2", "Tom3", "FloorTom0", "FloorTom1", "FloorTom2" };
        int rank = families.indexOf (familyOf (slot, element)) * 100;
        if (slot.contains ("LeftKick")) rank += 1;
        for (int t = 0; t < toms.size(); ++t)
            if (slot.endsWith (toms[t]) && ! (toms[t].startsWith ("Tom") && slot.containsIgnoreCase ("floor")))
                rank += t;
        return rank;
    };

    std::vector<juce::ValueTree> slots;
    for (const auto& slot : midiMap)
        if (elements.empty() || elements.count (slot.getType().toString()))
            slots.push_back (slot);
    std::stable_sort (slots.begin(), slots.end(), [&] (const juce::ValueTree& a, const juce::ValueTree& b)
    {
        const auto an = a.getType().toString(), bn = b.getType().toString();
        return order (an, elements[an]) < order (bn, elements[bn]);
    });
    if (slots.empty())
    {
        error = "The MODO DRUM kit is empty.";
        return {};
    }

    KitMap m;
    m.kitName = tree.getChildWithName ("PROJECT")["presetName"].toString();
    std::map<juce::String, int> counts;
    juce::StringArray names;
    for (const auto& slot : slots)
    {
        const auto slotName = slot.getType().toString();
        const auto element = elements.count (slotName) && elements[slotName].isNotEmpty()
                                 ? elements[slotName] : slotName.fromFirstOccurrenceOf ("Slot", false, false);
        const juce::String family = familyOf (slotName, element);
        auto name = modoName (element);
        for (int n = 2; names.contains (name); ++n)
            name = modoName (element) + " " + juce::String (n);
        names.add (name);
        std::map<juce::String, int> notes;   // MODO's articulation -> its note
        for (const auto& artic : slot)
            notes[artic.getType().toString()] = (int) artic["MIDINote"];

        const int firstZone = m.zones.size();
        std::pair<int, int> snaresOff { 0, 0 };
        std::set<juce::String> used;
        auto add = [&] (const juce::String& piece, const juce::String& label, const juce::String& artic, bool on)
        {
            if (auto it = notes.find (artic); it != notes.end())
            {
                m.zones.add ({ piece, label, it->second, -1, on, name + ": " + artic });
                used.insert (artic);
            }
        };

        juce::String piece, head;
        if (family.isEmpty())
        {
            piece = name;
            head = "PERC " + element.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
            static const char* percZones[] = { "MAIN", "SECONDARY", "ADD1", "ADD2" };
            int zone = 0;
            for (const auto& [artic, note] : notes)
                if (zone < 4)
                {
                    add (piece, head + " " + percZones[zone], artic, zone == 0);
                    ++zone;
                }
        }
        else if (family == "hats")
        {
            // MODO DRUM plays each openness from its own note: Tip (bow) and Shank (edge).
            piece = "Hi-Hat";
            head = "HATS";
            for (const auto& [area, label] : { std::pair { "Tip", "BOW" }, std::pair { "Shank", "EDGE" } })
            {
                add (piece, juce::String ("HATS ") + label + " CLOSED", juce::String ("Closed") + area, true);
                add (piece, juce::String ("HATS ") + label + " OPEN2", juce::String ("HalfOpen") + area, true);
                add (piece, juce::String ("HATS ") + label + " OPEN4", juce::String ("Open") + area, true);
            }
            add (piece, "HATS PEDAL CLOSED", "FootClosed", true);
            add (piece, "HATS FOOT SPLASH", "FootOpen", true);
        }
        else
        {
            const int n = ++counts[family];
            const juce::String num (n);
            piece = family == "kick" ? (n == 1 ? juce::String ("Kick") : "Kick " + num)
                  : family == "snare" ? (n == 1 ? juce::String ("Snare") : "Snare " + num)
                  : family.substring (0, 1).toUpperCase() + family.substring (1) + " " + num;
            head = family.toUpperCase() + num;
            // MODO DRUM's articulations -> the Mimic's zones, and which are ticked to start with.
            // Each hand has its own note; the left hand's is recorded, the right's is there to tick.
            struct Zone { const char* modo; const char* zone; bool on; };
            static const std::map<juce::String, std::vector<Zone>> zonesOf {
                { "kick", { { "Beater", "", true }, { "BeaterBuried", "BURIED", false } } },
                { "snare", { { "SnareL", "CENTER", true }, { "RimShotL", "RIMSHOT", true }, { "SideStick", "SIDESTICK", true },
                             { "SnareR", "CENTER R", false }, { "RimShotR", "RIMSHOT R", false } } },
                { "tom", { { "TomL", "CENTER", true }, { "TomR", "CENTER R", false } } },
                { "ride", { { "Edge", "BOW", true }, { "Bell", "BELL", true }, { "Catch", "CHOKE", false } } },
                { "crash", { { "Edge", "EDGE", true }, { "Bell", "BELL", false }, { "Catch", "CHOKE", false } } },
            };
            const auto& table = zonesOf.count (family) ? zonesOf.at (family) : zonesOf.at ("crash");
            for (const auto& z : table)
                add (piece, juce::String (z.zone).isEmpty() ? head : head + " " + z.zone, z.modo, z.on);
        }
        for (const auto& [artic, note] : notes)   // anything else: there to listen to and tick
            if (used.count (artic) == 0)
                add (piece, head + " " + artic.toUpperCase(), artic, false);
        for (int z = firstZone; z < m.zones.size(); ++z)
        {
            auto& zone = m.zones.getReference (z);
            zone.instrument = "MODO DRUM/" + element;
            zone.name = name;
            zone.library = "MODO DRUM";
        }
    }
    return m;
}

juce::String KitMap::ugritoneTextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        static const std::string open = "<Root ", close = "</Root>", panel = "Loadedpresetname0panels";
        const auto at = std::search (bytes, bytes + size, open.begin(), open.end());
        if (at == bytes + size)
            return {};
        const auto end = std::search (at, bytes + size, close.begin(), close.end());
        if (end == bytes + size || std::search (at, end, panel.begin(), panel.end()) == end)
            return {};
        return juce::String::fromUTF8 (at, (int) (end - at) + (int) close.size());
    });
}

KitMap KitMap::fromUgritone (const juce::String& text, juce::String& error)
{
    const auto xml = juce::parseXML (text);
    if (xml == nullptr || ! xml->hasTagName ("Root") || ! xml->hasAttribute ("PanelName0panels"))
    {
        error = "This isn't an Ugritone state.";
        return {};
    }

    // The MIDI map's notes come first in AllUserParam (as note / 127), in the order of
    // Ugritone's Midinote Settings matrix.
    static const juce::StringArray noteSlots { "kick", "tom1", "tom2", "tom3", "tom4", "tom5", "snare",
                                               "hihat closed", "hihat pedal", "hihat semiopen", "hihat wideopen",
                                               "crash1", "crash1choke", "crash2", "crash2choke", "splash1",
                                               "fxcymbal1", "fxcymbal2", "china1", "china2", "ridetop", "ridebell", "ridecrash" };
    std::map<juce::String, int> notes;
    if (auto* params = xml->getChildByName ("AllUserParam"))
    {
        const auto values = juce::StringArray::fromTokens (params->getAllSubText(), ",", {});
        for (int i = 0; i < noteSlots.size() && i < values.size(); ++i)
            notes[noteSlots[i]] = juce::roundToInt (values[i].getDoubleValue() * 127.0);
    }
    auto noteOf = [&] (const juce::String& slot) { return notes.count (slot) ? notes.at (slot) : 0; };

    // "/mnt/.../Soundbanks/KVLT Drums II Data/Presets/OSDM/ModernDeathMetal.xml" -> "KVLT Drums II"
    auto library = xml->getStringAttribute ("LastLoadedPresetFile").replaceCharacter ('\\', '/')
                       .fromFirstOccurrenceOf ("/Soundbanks/", false, true).upToFirstOccurrenceOf ("/", false, false)
                       .upToLastOccurrenceOf (" Data", false, true).trim();
    if (library.isEmpty())
        library = "Ugritone";

    KitMap m;
    m.kitName = xml->getStringAttribute ("curkitpresetname");
    for (int p = 0; xml->hasAttribute ("PanelName" + juce::String (p) + "panels"); ++p)
    {
        const auto suffix = juce::String (p) + "panels";
        const auto panel = xml->getStringAttribute ("PanelName" + suffix).toUpperCase();
        const auto drum = xml->getStringAttribute ("Loadedpresetname" + suffix);
        if (drum.isEmpty() || drum == "Disabled" || xml->getIntAttribute ("vis" + suffix, 1) == 0)
            continue;
        const auto number = panel.retainCharacters ("0123456789");
        const auto n = number.isEmpty() ? 1 : number.getIntValue();
        const int firstZone = m.zones.size();
        auto add = [&] (const juce::String& piece, const juce::String& label, int note, bool on, int cc = -1)
        {
            m.zones.add ({ piece, label, note, cc, on && note > 0 });
        };

        const auto lower = panel.toLowerCase();
        if (panel == "KICK")
            add ("Kick", "KICK1", noteOf ("kick"), true);
        else if (panel == "SNARE")
            add ("Snare", "SNARE1 CENTER", noteOf ("snare"), true);
        else if (panel.startsWith ("TOM"))
            add ("Tom " + juce::String (n), "TOM" + juce::String (n) + " CENTER", noteOf (lower), true);
        else if (panel == "HIHAT")
        {
            add ("Hi-Hat", "HATS BOW CLOSED", noteOf ("hihat closed"), true);
            add ("Hi-Hat", "HATS BOW OPEN2", noteOf ("hihat semiopen"), true);
            add ("Hi-Hat", "HATS BOW OPEN4", noteOf ("hihat wideopen"), true);
            add ("Hi-Hat", "HATS PEDAL CLOSED", noteOf ("hihat pedal"), true);
        }
        else if (panel.startsWith ("RIDE"))
        {
            add ("Ride", "RIDE1 BOW", noteOf ("ridetop"), true);
            add ("Ride", "RIDE1 BELL", noteOf ("ridebell"), true);
            add ("Ride", "RIDE1 EDGE", noteOf ("ridecrash"), true);
        }
        else
        {
            const auto family = panel.startsWith ("CRASH") ? "CRASH" : panel.startsWith ("SPLASH") ? "SPLASH"
                              : panel.startsWith ("CHINA") ? "CHINA" : "CYMBAL";
            const auto piece = panel.startsWith ("FX") ? "FX Cymbal " + juce::String (n)
                                                       : juce::String (family).substring (0, 1) + juce::String (family).substring (1).toLowerCase()
                                                             + " " + juce::String (n);
            const auto head = family + juce::String (n);
            add (piece, head + " EDGE", noteOf (lower), true);
            if (notes.count (lower + "choke"))
                add (piece, head + " CHOKE", noteOf (lower + "choke"), false);
        }

        for (int z = firstZone; z < m.zones.size(); ++z)
        {
            auto& zone = m.zones.getReference (z);
            zone.instrument = "Ugritone/" + drum;
            zone.name = drum;
            zone.library = library;
        }
    }
    if (m.zones.isEmpty())
        error = "The Ugritone kit has no drums loaded.";
    return m;
}

juce::String KitMap::soundworksTextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        // DW Soundworks keeps its kit as a <PDPreset>: the instruments come first, under the
        // first <InstDef>, then thousands of mixer parameters this doesn't need.
        static const std::string open = "<PDPreset>", close = "</InstDef>";
        const auto at = std::search (bytes, bytes + size, open.begin(), open.end());
        if (at == bytes + size)
            return {};
        const auto end = std::search (at, bytes + size, close.begin(), close.end());
        if (end == bytes + size)
            return {};
        return juce::String::fromUTF8 (at, (int) (end - at) + (int) close.size()) + "</PDPreset>";
    });
}

KitMap KitMap::fromSoundworks (const juce::String& text, juce::String& error)
{
    const auto xml = juce::parseXML (text);
    auto* instruments = xml != nullptr && xml->hasTagName ("PDPreset") ? xml->getChildByName ("InstDef") : nullptr;
    if (instruments == nullptr)
    {
        error = "This isn't a DW Soundworks state.";
        return {};
    }

    // Each mixer channel is one drum; its <Instrument>s are the drum's articulations, each on
    // its own note (-1 = none). Channels with no drum loaded have no InstrumentName.
    struct Artic { juce::String name; int note; };
    struct Drum { juce::String channel, drum, library; std::vector<Artic> artics; bool listenOnly = false; };
    std::vector<Drum> drums;
    std::map<int, size_t> byMixer;
    for (auto* e : instruments->getChildWithTagNameIterator ("Instrument"))
    {
        const auto drum = e->getStringAttribute ("InstrumentName").trim();
        if (drum.isEmpty())
            continue;
        const int mixer = e->getIntAttribute ("Mixer");
        if (byMixer.count (mixer) == 0)
        {
            byMixer[mixer] = drums.size();
            drums.push_back ({ e->getStringAttribute ("MixerName").trim(), drum, e->getStringAttribute ("LibName").trim(), {}, false });
        }
        drums[byMixer[mixer]].artics.push_back ({ e->getStringAttribute ("Name").trim(), e->getIntAttribute ("Midi", -1) });
    }
    if (drums.empty())
    {
        error = "The DW Soundworks kit has no drums loaded.";
        return {};
    }

    auto familyOf = [] (const Drum& d)
    {
        for (const auto& n : { d.channel.toLowerCase(), d.drum.toLowerCase() })
        {
            if (n.contains ("kick")) return "kick";
            if (n.contains ("snare")) return "snare";
            if (n.contains ("hat")) return "hats";
            if (n.contains ("tom") || n.contains ("floor")) return "tom";
            if (n.contains ("ride")) return "ride";
            if (n.contains ("splash")) return "splash";
            if (n.contains ("china")) return "china";
            if (n.contains ("stack")) return "stack";
            if (n.contains ("crash") || n.contains ("cymbal")) return "crash";
        }
        return "";
    };

    // A snare's snares-off ("SO") articulations are another snare sound: another piece, after
    // the kit's own drums, there to listen to and tick.
    const auto ownDrums = drums.size();
    for (size_t i = 0; i < ownDrums; ++i)
        if (juce::String (familyOf (drums[i])) == "snare")
        {
            Drum off { drums[i].channel, drums[i].drum + " Snares Off", drums[i].library, {}, true };
            auto& artics = drums[i].artics;
            for (auto a = artics.begin(); a != artics.end();)
                if (a->name.endsWithIgnoreCase (" SO"))
                {
                    off.artics.push_back ({ a->name.dropLastCharacters (3), a->note });
                    a = artics.erase (a);
                }
                else
                {
                    ++a;
                }
            if (! off.artics.empty())
                drums.push_back (std::move (off));
        }

    KitMap m;
    m.kitName = drums.front().library;
    std::map<juce::String, std::set<int>> numbers;
    juce::StringArray names;
    for (const auto& d : drums)
    {
        const juce::String family = familyOf (d);
        const int firstZone = m.zones.size();
        std::set<juce::String> used;
        auto add = [&] (const juce::String& piece, const juce::String& label, const juce::String& artic, bool on)
        {
            for (const auto& a : d.artics)
                if (a.name.equalsIgnoreCase (artic) && a.note >= 0 && used.insert (a.name).second)
                    m.zones.add ({ piece, label, a.note, -1, on && ! d.listenOnly,
                                   d.channel + ": " + a.name + (d.listenOnly ? " SO" : "") });
        };
        auto upper = [] (const juce::String& s) { return s.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "); };

        if (family.isEmpty())
        {
            // Percussion: a Mimic percussion instrument takes the first four articulations.
            static const char* percZones[] = { "MAIN", "SECONDARY", "ADD1", "ADD2" };
            // Several channels can share a name ("Cowbell"): each becomes its own piece.
            auto& taken = numbers["perc " + d.channel];
            int n = 1;
            while (taken.count (n) > 0)
                ++n;
            taken.insert (n);
            const auto piece = n == 1 ? d.channel : d.channel + " " + juce::String (n);
            const auto head = "PERC " + upper (piece).removeCharacters (" ");
            int zone = 0;
            for (const auto& a : d.artics)
                if (zone < 4 && a.note >= 0)
                {
                    add (piece, head + " " + percZones[zone], a.name, zone == 0);
                    ++zone;
                }
        }
        else if (family == "hats")
        {
            // DW plays each openness and stick area from its own note: Tighter, Tight, Normal and
            // Loose Closed, then Open 1 to 4, each on Edge, Bow and Bell. The Mimic's hat takes
            // bow and edge at five openings, closed to open.
            static const std::pair<const char*, const char*> steps[] {
                { "Normal Closed", "CLOSED" }, { "Open 1", "OPEN1" }, { "Open 2", "OPEN2" }, { "Open 3", "OPEN3" }, { "Open 4", "OPEN4" },
                { "Tight Closed", "TIGHT" }, { "Tighter Closed", "TIGHT2" }, { "Loose Closed", "CLOSED2" },
            };
            for (const auto& [area, label] : { std::pair { "Bow", "BOW" }, std::pair { "Edge", "EDGE" }, std::pair { "Bell", "BELL" } })
                for (const auto& [dw, step] : steps)
                    add ("Hi-Hat", juce::String ("HATS ") + label + " " + step, juce::String (dw) + " " + area,
                         juce::String (area) != "Bell" && ! juce::String (step).startsWith ("TIGHT") && juce::String (step) != "CLOSED2");
            add ("Hi-Hat", "HATS PEDAL CLOSED", "Pedal Closed", true);
            add ("Hi-Hat", "HATS FOOT SPLASH", "Foot Splash", true);
        }
        else
        {
            // Cymbals keep DW's number ("Crash 4") when it is free; drums are numbered in kit order,
            // so rack and floor toms run Tom 1, 2, 3...
            auto& taken = numbers[family];
            const bool cymbal = family != "kick" && family != "snare" && family != "tom";
            int n = cymbal ? d.channel.getTrailingIntValue() : 0;
            if (n <= 0 || taken.count (n) > 0)
                for (n = 1; taken.count (n) > 0; ++n) {}
            taken.insert (n);
            const juce::String num (n);
            const juce::String piece = family == "kick" ? (n == 1 ? juce::String ("Kick") : "Kick " + num)
                                     : family == "snare" ? (n == 1 ? juce::String ("Snare") : "Snare " + num)
                                     : family.substring (0, 1).toUpperCase() + family.substring (1) + " " + num;
            const auto head = family.toUpperCase() + num;
            // DW's articulation names -> the Mimic's zones, and which are ticked to start with.
            struct Zone { const char* dw; const char* zone; bool on; };
            static const std::map<juce::String, std::vector<Zone>> zonesOf {
                { "kick", { { "Center", "", true } } },
                { "snare", { { "Center", "CENTER", true }, { "Rimshot", "RIMSHOT", true }, { "X Stick", "SIDESTICK", true },
                             { "Rim", "RIMCLICK", true }, { "Edge", "EDGE", false } } },
                { "tom", { { "Center", "CENTER", true }, { "Rimshot", "RIMSHOT", true }, { "Rim", "RIMCLICK", true } } },
                { "ride", { { "Bow", "BOW", true }, { "Bell", "BELL", true }, { "Edge", "EDGE", true } } },
                { "crash", { { "Edge", "EDGE", true }, { "Bow", "BOW", true }, { "Bell", "BELL", false } } },
            };
            const auto& table = zonesOf.count (family) ? zonesOf.at (family) : zonesOf.at ("crash");
            for (const auto& z : table)
                add (piece, juce::String (z.zone).isEmpty() ? head : head + " " + z.zone, z.dw, z.on);
            for (const auto& a : d.artics)   // anything else: there to listen to and tick
                if (a.note >= 0 && used.count (a.name) == 0)
                    add (piece, head + " " + upper (a.name), a.name, false);
        }

        auto name = d.drum;
        for (int n = 2; names.contains (name); ++n)
            name = d.drum + " " + juce::String (n);
        names.add (name);
        for (int z = firstZone; z < m.zones.size(); ++z)
        {
            auto& zone = m.zones.getReference (z);
            zone.instrument = "DW Soundworks/" + d.drum;
            zone.name = name;
            zone.library = d.library.isNotEmpty() ? d.library : juce::String ("DW Soundworks");
        }
    }
    // DW can put several drums on one note and plays them all together, so a recording of it is
    // their mix. Only the first drum on a note starts ticked.
    std::map<int, const KitZone*> firstOnNote;
    for (auto& zone : m.zones)
    {
        const auto [first, isNew] = firstOnNote.insert ({ zone.note, &zone });
        if (! isNew && first->second->piece != zone.piece)
        {
            zone.enabled = false;
            zone.pluginName << " (note shared with " << first->second->name << ")";
        }
    }
    if (m.zones.isEmpty())
        error = "The DW Soundworks kit has no notes mapped.";
    return m;
}

juce::String KitMap::mlDrumsTextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        static const std::string open = "<PARAMS ", close = "</PARAMS>", key = "midikey___0Kick1Key";
        const auto at = std::search (bytes, bytes + size, open.begin(), open.end());
        if (at == bytes + size)
            return {};
        const auto end = std::search (at, bytes + size, close.begin(), close.end());
        if (end == bytes + size || std::search (at, end, key.begin(), key.end()) == end)
            return {};
        return juce::String::fromUTF8 (at, (int) (end - at) + (int) close.size());
    });
}

KitMap KitMap::fromMlDrums (const juce::String& text, juce::String& error)
{
    const auto xml = juce::parseXML (text);
    if (xml == nullptr || ! xml->hasTagName ("PARAMS") || ! xml->hasAttribute ("midikey___0Kick1Key"))
    {
        error = "This isn't an ML Drums state.";
        return {};
    }
    // Each articulation has two keys ("...1Key", "...2Key"); -1 = none.
    auto noteOf = [&] (const juce::String& name)
    {
        const auto first = xml->getIntAttribute ("midikey___0" + name + "1Key", -1);
        return first >= 0 ? first : xml->getIntAttribute ("midikey___0" + name + "2Key", -1);
    };

    // The slots in the order of the state's instrument0..18, each holding the ID of the drum
    // it plays. That order is inferred from the MIDI map's own order (19 slots, 19 IDs).
    struct Slot
    {
        const char* key;    // ML Drums' name in its MIDI map
        const char* piece;
        const char* head;   // zone label start
    };
    static const Slot slots[] = {
        { "Kick", "Kick", "KICK1" }, { "Snare", "Snare", "SNARE1" },
        { "RackTom1", "Tom 1", "TOM1" }, { "RackTom2", "Tom 2", "TOM2" }, { "RackTom3", "Tom 3", "TOM3" },
        { "FloorTom1", "Tom 4", "TOM4" }, { "FloorTom2", "Tom 5", "TOM5" },
        { "Hihat", "Hi-Hat", "HATS" }, { "Ride", "Ride", "RIDE1" },
        { "Crash1", "Crash 1", "CRASH1" }, { "Crash2", "Crash 2", "CRASH2" }, { "Crash3", "Crash 3", "CRASH3" },
        { "ChinaL", "China 1", "CHINA1" }, { "ChinaR", "China 2", "CHINA2" },
        { "Splash1", "Splash 1", "SPLASH1" }, { "Splash2", "Splash 2", "SPLASH2" },
        { "Stax", "Stack 1", "STACK1" }, { "MiniChina", "China 3", "CHINA3" }, { "Bell", "Bell", "BELL1" },
    };

    KitMap m;
    m.kitName = "ML Drums";
    for (int s = 0; s < (int) std::size (slots); ++s)
    {
        const auto& slot = slots[s];
        const juce::String key (slot.key), head (slot.head);
        const int firstZone = m.zones.size();
        auto add = [&] (const juce::String& label, const juce::String& noteName, bool on = true)
        {
            const auto note = noteOf (noteName);
            if (note >= 0)
                m.zones.add ({ slot.piece, label, note, -1, on });
        };

        if (key == "Kick")
            add (head, "Kick");
        else if (key == "Snare")
        {
            add (head + " CENTER", "Snare");
            add (head + " SIDESTICK", "SnareSideStick");
        }
        else if (key.contains ("Tom"))
            add (head + " CENTER", key);
        else if (key == "Hihat")
        {
            // Openness is a note of its own in ML Drums, not the pedal (CC4).
            add ("HATS BOW CLOSED", "HihatTip");
            add ("HATS EDGE CLOSED", "HihatEdge");
            add ("HATS EDGE OPEN1", "HihatSemiOpen");
            add ("HATS EDGE OPEN2", "HihatSemiOpen2");
            add ("HATS EDGE OPEN3", "HihatSemiOpen3");
            add ("HATS EDGE OPEN4", "HihatOpen");
            add ("HATS PEDAL CLOSED", "HihatPedal");
        }
        else if (key == "Ride")
        {
            add (head + " BOW", "Ride");
            add (head + " BELL", "RideBell");
            add (head + " EDGE", "RideEdge");
        }
        else
        {
            const auto name = key == "Crash3" ? juce::String ("Crash3Midi") : key;   // as ML Drums spells it
            add (head + " EDGE", name, key != "Bell");
            add (head + " CHOKE", key + "Choke", false);
        }

        const auto id = xml->getStringAttribute ("instrument" + juce::String (s));
        for (int z = firstZone; z < m.zones.size(); ++z)
        {
            auto& zone = m.zones.getReference (z);
            zone.instrument = "ML Drums/" + key + (id.isNotEmpty() ? "#" + id : juce::String());
            zone.library = "ML Drums";
        }
    }
    if (m.zones.isEmpty())
        error = "The ML Drums MIDI map has no notes.";
    return m;
}

juce::String KitMap::addictiveTextIn (const juce::MemoryBlock& data)
{
    return findInState (data, [] (const char* bytes, size_t size) -> juce::String
    {
        // AD2 keeps its settings as "key\0value\0" pairs after a "COTTAD2State" header.
        static const std::string marker = "COTTAD2State";
        const auto at = std::search (bytes, bytes + size, marker.begin(), marker.end());
        if (at == bytes + size)
            return {};
        std::string text (at + marker.size(), bytes + size);
        std::replace (text.begin(), text.end(), '\0', '\n');
        return juce::String::fromUTF8 (text.data(), (int) text.size());
    });
}

KitMap KitMap::fromAddictive (const juce::String& text, juce::String& error)
{
    std::map<juce::String, juce::String> values;
    const auto lines = juce::StringArray::fromLines (text);
    for (int i = 0; i + 1 < lines.size(); i += 2)
        values[lines[i]] = lines[i + 1];
    if (values.count ("StateProductName") == 0 && values.count ("MidiFilterMap_MAP_KeyMap_036") == 0)
    {
        error = "This isn't an Addictive Drums 2 state.";
        return {};
    }
    auto value = [&] (const juce::String& key) { return values.count (key) ? values.at (key) : juce::String(); };

    // The key map gives each note the articulation it plays, by AD2's own number for it (the
    // same in every map: 1 is the kick, 3 the snare's open hit...). A few articulations are on
    // two notes in AD2 Standard (Ride 1 Tip on 45 and 60); the higher one is used.
    std::map<int, int> noteOf;
    for (int note = 0; note < 128; ++note)
        if (const int artic = value ("MidiFilterMap_MAP_KeyMap_" + juce::String (note).paddedLeft ('0', 3)).getIntValue(); artic > 0)
            noteOf[artic] = note;

    // "app____Black Oyster__Black Room.AD2Preset" -> "Black Oyster"
    juce::StringArray presetParts;
    presetParts.addTokens (value ("Layer1/CurrentPresetId").replace ("__", "\n"), "\n", {});
    presetParts.removeEmptyStrings();
    const auto library = presetParts.size() >= 3 ? presetParts[presetParts.size() - 2] : juce::String ("Addictive Drums 2");

    KitMap m;
    m.kitName = value ("Layer1/CurrentPresetName");
    struct Zone { int artic; const char* label; const char* name; bool on; };
    auto addSlot = [&] (const juce::String& slot, const juce::String& piece, std::initializer_list<Zone> zones)
    {
        const auto drum = value ("Sampler_" + slot + "_KitPieceSelection");
        if (drum.getIntValue() == 0)
            return;
        const int first = m.zones.size();
        for (const auto& z : zones)
            if (noteOf.count (z.artic))
                m.zones.add ({ piece, z.label, noteOf.at (z.artic), -1, z.on, piece + ": " + z.name });
        for (int i = first; i < m.zones.size(); ++i)
        {
            auto& zone = m.zones.getReference (i);
            zone.instrument = "Addictive Drums 2/" + drum;
            zone.library = library;
        }
    };

    addSlot ("KICK", "Kick", { { 1, "KICK1", "Kick", true } });
    addSlot ("SNAR", "Snare", { { 3, "SNARE1 CENTER", "Open Hit", true }, { 2, "SNARE1 RIMSHOT", "RimShot", true },
                                { 5, "SNARE1 SIDESTICK", "SideStick", true }, { 7, "SNARE1 RIMCLICK", "RimClick", true },
                                { 4, "SNARE1 RIMSHOT SHALLOW", "Shallow RimShot", true }, { 6, "SNARE1 EDGE", "Shallow Hit", false } });
    // AD2's open hat (Open A to D) is one sound per opening; the Mimic's hat wants it for the
    // bow and the edge, so both take it.
    addSlot ("HHAT", "Hi-Hat", { { 10, "HATS BOW CLOSED", "Closed1 Tip", true }, { 11, "HATS EDGE CLOSED", "Closed1 Shaft", true },
                                 { 15, "HATS BOW OPEN1", "Open A", true }, { 16, "HATS BOW OPEN2", "Open B", true },
                                 { 17, "HATS BOW OPEN3", "Open C", true }, { 18, "HATS BOW OPEN4", "Open D", true },
                                 { 15, "HATS EDGE OPEN1", "Open A", true }, { 16, "HATS EDGE OPEN2", "Open B", true },
                                 { 17, "HATS EDGE OPEN3", "Open C", true }, { 18, "HATS EDGE OPEN4", "Open D", true },
                                 { 9, "HATS PEDAL CLOSED", "Foot Close", true }, { 20, "HATS FOOT SPLASH", "Foot Splash", true },
                                 { 12, "HATS BOW CLOSED2", "Closed2 Tip", false }, { 13, "HATS EDGE CLOSED2", "Closed2 Shaft", false },
                                 { 14, "HATS BELL CLOSED", "Closed Bell", false }, { 19, "HATS BELL OPEN4", "Open Bell", false } });
    // AD2's Tom 1 is the highest.
    static const char* tomLabels[][2] { { "TOM1 CENTER", "TOM1 RIMSHOT" }, { "TOM2 CENTER", "TOM2 RIMSHOT" },
                                        { "TOM3 CENTER", "TOM3 RIMSHOT" }, { "TOM4 CENTER", "TOM4 RIMSHOT" } };
    for (int t = 0; t < 4; ++t)
        addSlot ("TOM" + juce::String (t + 1), "Tom " + juce::String (t + 1),
                 { { 31 - 2 * t, tomLabels[t][0], "Open Hit", true }, { 32 - 2 * t, tomLabels[t][1], "Rimshot", true } });
    addSlot ("RID1", "Ride 1", { { 21, "RIDE1 BOW", "Tip", true }, { 22, "RIDE1 BELL", "Bell", true },
                                 { 23, "RIDE1 EDGE", "Shaft", true }, { 24, "RIDE1 CHOKE", "Choke", false } });
    addSlot ("RID2", "Ride 2", { { 62, "RIDE2 BOW", "Tip", true }, { 63, "RIDE2 BELL", "Bell", true },
                                 { 64, "RIDE2 EDGE", "Shaft", true }, { 65, "RIDE2 CHOKE", "Choke", false } });
    // The state doesn't say which cymbals are crashes, chinas or splashes: they come in as crashes.
    static const int cymbals[][2] { { 34, 35 }, { 36, 37 }, { 38, 39 }, { 67, 68 }, { 69, 70 }, { 71, 72 } };
    static const char* cymbalLabels[][2] { { "CRASH1 EDGE", "CRASH1 CHOKE" }, { "CRASH2 EDGE", "CRASH2 CHOKE" }, { "CRASH3 EDGE", "CRASH3 CHOKE" },
                                           { "CRASH4 EDGE", "CRASH4 CHOKE" }, { "CRASH5 EDGE", "CRASH5 CHOKE" }, { "CRASH6 EDGE", "CRASH6 CHOKE" } };
    for (int c = 0; c < 6; ++c)
        addSlot ("CYM" + juce::String (c + 1), "Cymbal " + juce::String (c + 1),
                 { { cymbals[c][0], cymbalLabels[c][0], "Hit", true }, { cymbals[c][1], cymbalLabels[c][1], "Choke", false } });
    // Flexi slots hold anything (percussion, extra snares, stacks); each is a percussion piece.
    static const int flexi[][4] { { 8, 59, 60, 61 }, { 73, 74, 75, 76 }, { 77, 78, 79, 80 } };
    static const char* flexiLabels[][4] { { "PERC FLEXI1 MAIN", "PERC FLEXI1 SECONDARY", "PERC FLEXI1 ADD1", "PERC FLEXI1 ADD2" },
                                          { "PERC FLEXI2 MAIN", "PERC FLEXI2 SECONDARY", "PERC FLEXI2 ADD1", "PERC FLEXI2 ADD2" },
                                          { "PERC FLEXI3 MAIN", "PERC FLEXI3 SECONDARY", "PERC FLEXI3 ADD1", "PERC FLEXI3 ADD2" } };
    for (int f = 0; f < 3; ++f)
        addSlot ("FLX" + juce::String (f + 1), "Flexi " + juce::String (f + 1),
                 { { flexi[f][0], flexiLabels[f][0], "Hit A", true }, { flexi[f][1], flexiLabels[f][1], "Hit B", true },
                   { flexi[f][2], flexiLabels[f][2], "Hit C", true }, { flexi[f][3], flexiLabels[f][3], "Hit D", true } });

    if (m.zones.isEmpty())
        error = "The Addictive Drums 2 kit has no drums loaded.";
    return m;
}

KitMap KitMap::fromMdlTone (const juce::String& text, juce::String& error)
{
    auto lines = juce::StringArray::fromLines (text.trim());
    if (lines.size() < 37 || lines[0].trim() != "%mapping")
    {
        error = "This isn't an MDL Tone MIDI mapping.";
        return {};
    }
    lines.remove (0);

    // The array's slots, in the order the library's MIDI Mapping page saves them (checked against
    // Alexandre's export and the page's own note list, 2026-10-10). "Close" is a plain hit.
    struct Slot { const char* piece; const char* label; const char* name; bool on; };
    static const Slot slots[] {
        { "Kick", "KICK1", "Kick 1", true }, { "Snare", "SNARE1 CENTER", "Snare", true },
        { "Tom 1", "TOM1 CENTER", "Tom 1", true }, { "Tom 2", "TOM2 CENTER", "Tom 2", true },
        { "Tom 3", "TOM3 CENTER", "Tom 3", true }, { "Tom 4", "TOM4 CENTER", "Tom 4", true },
        { "Tom 5", "TOM5 CENTER", "Tom 5", true }, { "Tom 6", "TOM6 CENTER", "Tom 6", true },
        { "China 1", "CHINA1 CHOKE", "China L Choke", false }, { "China 1", "CHINA1 EDGE", "China L Close", true },
        { "China 2", "CHINA2 CHOKE", "China R Choke", false }, { "China 2", "CHINA2 EDGE", "China R Close", true },
        { "Crash 1", "CRASH1 CHOKE", "Crash LL Choke", false }, { "Crash 1", "CRASH1 EDGE", "Crash LL Close", true },
        { "Crash 2", "CRASH2 CHOKE", "Crash L Choke", false }, { "Crash 2", "CRASH2 EDGE", "Crash L Close", true },
        { "Crash 3", "CRASH3 CHOKE", "Crash R Choke", false }, { "Crash 3", "CRASH3 EDGE", "Crash R Close", true },
        { "Hi-Hat", "HATS EDGE CLOSED", "Hat L Edge", true }, { "Hi-Hat", "HATS BOW CLOSED", "Hat L Tip", true },
        { "Hi-Hat", "HATS EDGE TIGHT", "Hat L Tight Edge", false }, { "Hi-Hat", "HATS BOW TIGHT", "Hat L Tight Tip", false },
        { "Hi-Hat", "HATS BOW OPEN2", "Hat L Open1", true }, { "Hi-Hat", "HATS BOW OPEN4", "Hat L Open2", true },
        { "Hi-Hat", "HATS PEDAL CLOSED", "Hat L Pedal", true },
        { "Hi-Hat 2", "HATS2 EDGE CLOSED", "Hat R Edge", false }, { "Hi-Hat 2", "HATS2 BOW CLOSED", "Hat R Tip", false },
        { "Ride 1", "RIDE1 BELL", "Ride L Bell", true }, { "Ride 1", "RIDE1 BOW", "Ride L Close", true },
        { "Ride 2", "RIDE2 BELL", "Ride R Bell", true }, { "Ride 2", "RIDE2 BOW", "Ride R Close", true },
        { "Splash 1", "SPLASH1 CHOKE", "Splash L Choke", false }, { "Splash 1", "SPLASH1 EDGE", "Splash L Close", true },
        { "Splash 2", "SPLASH2 CHOKE", "Splash R Choke", false }, { "Splash 2", "SPLASH2 EDGE", "Splash R Close", true },
        { "Kick 2", "KICK2", "Kick 2", false },
    };

    KitMap m;
    for (int i = 0; i < (int) std::size (slots); ++i)
    {
        const int note = lines[i].trim().getIntValue();
        if (lines[i].trim().isEmpty() || note < 0 || note > 127)
            continue;
        const auto& s = slots[i];
        KitZone zone { s.piece, s.label, note, -1, s.on, s.name };
        zone.instrument = juce::String ("MDL Tone Ultimate Heavy Drums/") + s.piece;
        zone.library = "Ultimate Heavy Drums";
        m.zones.add (zone);
    }
    // The open hat is one sound per opening, there for the bow and the edge alike.
    for (const auto& [from, to] : { std::pair { "HATS BOW OPEN2", "HATS EDGE OPEN2" }, std::pair { "HATS BOW OPEN4", "HATS EDGE OPEN4" } })
        for (const auto& z : m.zones)
            if (z.label == from)
            {
                auto copy = z;
                copy.label = to;
                m.zones.add (copy);
                break;
            }
    if (m.zones.isEmpty())
        error = "The MDL Tone mapping has no notes.";
    return m;
}

namespace
{
/** "C0", "A#-1", "C#3": Kontakt's note names, where C3 is note 60. */
int kontaktNote (const juce::String& name)
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto pitch = name.initialSectionNotContaining ("-0123456789");
    const int octave = name.substring (pitch.length()).getIntValue();
    return (octave + 2) * 12 + names.indexOf (pitch);
}

struct KontaktLibrary
{
    const char* name;
    const char* library;
    // One zone per line: piece|label|note|1 to record it. Notes are as the Mapping page shows them.
    const char* rows;
};

// The libraries' default "Mapping" pages (screenshots Alexandre sent, 2026-10-10). Their hi-hat
// "Closed/Open n" is the edge, "Tip ..." the bow; "choke hit" rows are left out.
const char* const eloySnareAndKick =
    "Kick|KICK1|C0|1\nKick 2|KICK2|B-1|0\n"
    "Snare|SNARE1 CENTER|D0|1\nSnare|SNARE1 OFF CENTER|C#0|0\nSnare|SNARE1 FLAM|D#0|0\n"
    "Snare|SNARE1 SIDESTICK|F#0|1\nSnare 2|SNARE2 CENTER|F0|0\n";
const char* const eloyHats =
    "Hi-Hat|HATS EDGE CLOSED|F#1|1\nHi-Hat|HATS EDGE OPEN1|G1|1\nHi-Hat|HATS EDGE OPEN2|G#1|1\n"
    "Hi-Hat|HATS EDGE OPEN3|A1|1\nHi-Hat|HATS EDGE OPEN4|A#1|1\nHi-Hat|HATS EDGE OPEN5|B1|1\n"
    "Hi-Hat|HATS BOW CLOSED|F1|1\nHi-Hat|HATS PEDAL CLOSED|C2|1\nHi-Hat|HATS FOOT SPLASH|C#2|1\n";
const char* const tomsOneToFour =
    "Tom 1|TOM1 CENTER|A0|1\nTom 2|TOM2 CENTER|A#0|1\nTom 3|TOM3 CENTER|B0|1\nTom 4|TOM4 CENTER|C1|1\n";
const char* const rideD3 =
    "Ride|RIDE1 BOW|D3|1\nRide|RIDE1 BELL|C#3|1\nRide|RIDE1 EDGE|D#3|1\n";

const std::vector<std::pair<KontaktLibrary, juce::String>>& kontaktLibraryTable()
{
    static const std::vector<std::pair<KontaktLibrary, juce::String>> table {
        { { "Eloy Drums - CasaGroove", "Eloy Drums", nullptr },
          juce::String (eloySnareAndKick) + tomsOneToFour + rideD3 + "Ride|RIDE1 CHOKE|C3|0\n" + eloyHats
            + "Hi-Hat|HATS BOW OPEN1|D2|1\nHi-Hat|HATS BOW OPEN2|D#2|1\nHi-Hat|HATS BOW OPEN3|E1|1\n"
              "Hi-Hat|HATS BOW OPEN4|D#1|1\nHi-Hat|HATS BOW OPEN5|D1|1\n"
              "Crash 1|CRASH1 EDGE|E2|1\nCrash 1|CRASH1 CHOKE|F2|0\nCrash 2|CRASH2 EDGE|F#2|1\nCrash 2|CRASH2 CHOKE|G2|0\n"
              "Crash 3|CRASH3 EDGE|G#2|1\nCrash 3|CRASH3 CHOKE|A2|0\nChina|CHINA1 EDGE|F3|1\nChina|CHINA1 CHOKE|F#3|0\n"
              "Splash|SPLASH1 EDGE|C#4|1\nSplash|SPLASH1 CHOKE|D4|0\nStack|STACK1 EDGE|D#4|1\nStack|STACK1 CHOKE|E4|0\n" },
        { { "Eloy Drums - CasaGrande", "Eloy Drums", nullptr },
          juce::String (eloySnareAndKick) + tomsOneToFour + "Tom 5|TOM5 CENTER|C#1|1\n" + rideD3 + "Ride|RIDE1 CHOKE|C3|0\n" + eloyHats
            + "Hi-Hat|HATS BOW OPEN1|D2|1\nHi-Hat|HATS BOW OPEN2|D#2|1\nHi-Hat|HATS BOW OPEN3|E1|1\n"
              "Hi-Hat|HATS BOW OPEN4|D#1|1\nHi-Hat|HATS BOW OPEN5|D1|1\n"
              "Crash 1|CRASH1 EDGE|E2|1\nCrash 1|CRASH1 CHOKE|F2|0\nCrash 2|CRASH2 EDGE|G#2|1\nCrash 2|CRASH2 CHOKE|A2|0\n"
              "Crash 3|CRASH3 EDGE|F#2|1\nCrash 3|CRASH3 CHOKE|G2|0\nCrash 4|CRASH4 EDGE|A#2|1\nCrash 4|CRASH4 CHOKE|B2|0\n"
              "China|CHINA1 EDGE|F3|1\nChina|CHINA1 CHOKE|F#3|0\n" },
        { { "Trivium Drums", "Trivium Drums", nullptr },
          juce::String (eloySnareAndKick) + tomsOneToFour + "Tom 5|TOM5 CENTER|C#1|1\n" + eloyHats
            + "Hi-Hat|HATS BOW OPEN1|D2|1\nHi-Hat|HATS BOW OPEN2|D#2|1\nHi-Hat|HATS BOW OPEN3|E1|1\n"
              "Hi-Hat 2|HATS2 EDGE CLOSED|A#4|0\nHi-Hat 2|HATS2 EDGE OPEN1|B4|0\nHi-Hat 2|HATS2 EDGE OPEN2|C5|0\n"
              "Hi-Hat 2|HATS2 EDGE OPEN3|C#5|0\n"
              "Ride 1|RIDE1 BOW|D3|1\nRide 1|RIDE1 BELL|C#3|1\nRide 1|RIDE1 EDGE|D#3|1\n"
              "Ride 2|RIDE2 BOW|C6|1\nRide 2|RIDE2 BELL|C#6|1\n"
              "Crash 1|CRASH1 EDGE|E2|1\nCrash 1|CRASH1 CHOKE|F2|0\nCrash 2|CRASH2 EDGE|F#2|1\nCrash 2|CRASH2 CHOKE|G2|0\n"
              "China 1|CHINA1 EDGE|F3|1\nChina 1|CHINA1 CHOKE|F#3|0\nChina 2|CHINA2 EDGE|G3|1\nChina 2|CHINA2 CHOKE|G#3|0\n"
              "Splash 1|SPLASH1 EDGE|C#4|1\nSplash 1|SPLASH1 CHOKE|D4|0\nSplash 2|SPLASH2 EDGE|D#4|1\nSplash 2|SPLASH2 CHOKE|E4|0\n" },
        { { "Krimh Drums", "Krimh Drums", nullptr },
          juce::String (eloySnareAndKick) + tomsOneToFour + "Tom 5|TOM5 CENTER|C#1|0\n" + rideD3 + eloyHats
            + "Hi-Hat|HATS BOW OPEN3|D2|1\n"
              "Hi-Hat 2|HATS2 EDGE CLOSED|A#4|0\nHi-Hat 2|HATS2 EDGE OPEN1|B4|0\nHi-Hat 2|HATS2 EDGE OPEN2|C5|0\n"
              "Hi-Hat 2|HATS2 EDGE OPEN3|C#5|0\nHi-Hat 2|HATS2 EDGE OPEN4|D5|0\nHi-Hat 2|HATS2 EDGE OPEN5|D#5|0\n"
              "Hi-Hat 2|HATS2 EDGE OPEN6|E5|0\n"
              "Crash 1|CRASH1 EDGE|E2|1\nCrash 1|CRASH1 CHOKE|F2|0\nCrash 2|CRASH2 EDGE|F#2|1\nCrash 2|CRASH2 CHOKE|G2|0\n"
              "Crash 3|CRASH3 EDGE|G#2|1\nCrash 3|CRASH3 CHOKE|A2|0\nCrash 4|CRASH4 EDGE|A#2|1\nCrash 4|CRASH4 CHOKE|B2|0\n"
              "Splash 1|SPLASH1 EDGE|C#4|1\nSplash 1|SPLASH1 CHOKE|D4|0\nSplash 2|SPLASH2 EDGE|D#4|1\nSplash 2|SPLASH2 CHOKE|E4|0\n"
              "China 1|CHINA1 EDGE|F3|1\nChina 1|CHINA1 CHOKE|F#3|0\nChina 2|CHINA2 EDGE|G3|1\nChina 2|CHINA2 CHOKE|G#3|0\n" },
        { { "GGD Invasion", "GGD Invasion", nullptr },
          juce::String ("Kick|KICK1|C0|1\nKick 2|KICK2|B-1|0\n"
              "Snare|SNARE1 CENTER|D0|1\nSnare|SNARE1 FLAM|D#0|0\nSnare|SNARE1 BUZZ|E0|0\nSnare|SNARE1 SIDESTICK|F#0|1\n"
              "Snare 2|SNARE2 CENTER|F0|0\n")
            + tomsOneToFour + "Tom 5|TOM5 CENTER|C#1|1\nTom 6|TOM6 CENTER|D1|1\n" + rideD3
            + "Hi-Hat|HATS BOW TIGHT|F1|1\nHi-Hat|HATS EDGE TIGHT|F#1|1\nHi-Hat|HATS BOW CLOSED|G1|1\nHi-Hat|HATS EDGE CLOSED|G#1|1\n"
              "Hi-Hat|HATS EDGE OPEN1|A1|1\nHi-Hat|HATS BOW OPEN1|A1|1\nHi-Hat|HATS EDGE OPEN2|A#1|1\nHi-Hat|HATS BOW OPEN2|A#1|1\n"
              "Hi-Hat|HATS EDGE OPEN3|B1|1\nHi-Hat|HATS BOW OPEN3|B1|1\nHi-Hat|HATS PEDAL CLOSED|C2|1\n"
              "Hi-Hat 2|HATS2 EDGE CLOSED|A#3|0\nHi-Hat 2|HATS2 EDGE OPEN1|B3|0\n"
              "Crash 1|CRASH1 EDGE|E2|1\nCrash 1|CRASH1 CHOKE|F2|0\nCrash 2|CRASH2 EDGE|F#2|1\nCrash 2|CRASH2 CHOKE|G2|0\n"
              "Crash 3|CRASH3 EDGE|G#2|1\nCrash 3|CRASH3 CHOKE|A2|0\nCrash 4|CRASH4 EDGE|A#2|1\nCrash 4|CRASH4 CHOKE|B2|0\n"
              "China 1|CHINA1 EDGE|F3|1\nChina 1|CHINA1 CHOKE|F#3|0\nChina 2|CHINA2 EDGE|G3|1\nChina 2|CHINA2 CHOKE|G#3|0\n"
              "Splash 1|SPLASH1 EDGE|C#4|1\nSplash 1|SPLASH1 CHOKE|D4|0\nSplash 2|SPLASH2 EDGE|D#4|1\nSplash 2|SPLASH2 CHOKE|E4|0\n" },
        // Odeholm's maps use other notes than the rest; its open hat is one sound per opening.
        { { "Odeholm Drums", "Odeholm Drums", nullptr },
          juce::String ("Kick|KICK1|C1|1\nKick 2|KICK2|B0|0\nSnare|SNARE1 CENTER|D1|1\nSnare|SNARE1 FLAM|D#1|0\n"
              "Tom 1|TOM1 CENTER|B1|1\nTom 2|TOM2 CENTER|A1|1\nTom 3|TOM3 CENTER|G1|1\nTom 4|TOM4 CENTER|F1|1\n"
              "Hi-Hat|HATS BOW CLOSED|D#2|1\nHi-Hat|HATS EDGE CLOSED|E2|1\nHi-Hat|HATS PEDAL CLOSED|F2|1\n"
              "Hi-Hat|HATS EDGE OPEN1|D2|1\nHi-Hat|HATS BOW OPEN1|D2|1\nHi-Hat|HATS EDGE OPEN2|C#2|1\nHi-Hat|HATS BOW OPEN2|C#2|1\n"
              "Hi-Hat|HATS EDGE OPEN3|C2|1\nHi-Hat|HATS BOW OPEN3|C2|1\n"
              "Ride|RIDE1 BOW|A#2|1\nRide|RIDE1 BELL|B2|1\nRide|RIDE1 EDGE|C3|1\n"
              "Crash 1|CRASH1 EDGE|F#2|1\nCrash 1|CRASH1 CHOKE|G2|0\nCrash 2|CRASH2 EDGE|G#2|1\nCrash 2|CRASH2 CHOKE|A2|0\n"
              "Crash 3|CRASH3 EDGE|C#3|1\nCrash 3|CRASH3 CHOKE|D3|0\n"
              "China 1|CHINA1 EDGE|D#3|1\nChina 1|CHINA1 CHOKE|E3|0\nChina 2|CHINA2 EDGE|F3|1\nChina 2|CHINA2 CHOKE|F#3|0\n"
              "Stack|STACK1 EDGE|G3|1\n"
              "Splash 1|SPLASH1 EDGE|G#3|1\nSplash 1|SPLASH1 CHOKE|A3|0\nSplash 2|SPLASH2 EDGE|A#3|1\nSplash 2|SPLASH2 CHOKE|B3|0\n"
              "Bell|PERC 1 HEAD|C4|0\n") },
        { { "Odeholm Drums (other map)", "Odeholm Drums", nullptr },
          juce::String ("Kick|KICK1|C1|1\nKick 2|KICK2|B0|0\nSnare|SNARE1 CENTER|D1|1\nSnare|SNARE1 FLAM|D#1|0\n"
              "Tom 1|TOM1 CENTER|C2|1\nTom 2|TOM2 CENTER|B1|1\nTom 3|TOM3 CENTER|A1|1\nTom 4|TOM4 CENTER|G1|1\n"
              "Hi-Hat|HATS BOW CLOSED|D#3|1\nHi-Hat|HATS EDGE CLOSED|C#3|1\nHi-Hat|HATS PEDAL CLOSED|A-1|1\n"
              "Hi-Hat|HATS EDGE OPEN1|A#-1|1\nHi-Hat|HATS BOW OPEN1|A#-1|1\nHi-Hat|HATS EDGE OPEN2|E3|1\nHi-Hat|HATS BOW OPEN2|E3|1\n"
              "Hi-Hat|HATS EDGE OPEN3|C3|1\nHi-Hat|HATS BOW OPEN3|C3|1\n"
              "Ride|RIDE1 BOW|D#2|1\nRide|RIDE1 BELL|F2|1\nRide|RIDE1 EDGE|B2|1\n"
              "Crash 1|CRASH1 EDGE|C#2|1\nCrash 1|CRASH1 CHOKE|D2|0\nCrash 2|CRASH2 EDGE|A2|1\nCrash 2|CRASH2 CHOKE|A#2|0\n"
              "Crash 3|CRASH3 EDGE|G3|1\nCrash 3|CRASH3 CHOKE|G#3|0\n"
              "China 1|CHINA1 EDGE|F3|1\nChina 1|CHINA1 CHOKE|F#3|0\nChina 2|CHINA2 EDGE|E2|1\nChina 2|CHINA2 CHOKE|F#2|0\n"
              "Stack|STACK1 EDGE|F0|1\n"
              "Splash 1|SPLASH1 EDGE|A3|1\nSplash 1|SPLASH1 CHOKE|A#3|0\nSplash 2|SPLASH2 EDGE|G2|1\nSplash 2|SPLASH2 CHOKE|G#2|0\n"
              "Bell|PERC 1 HEAD|B3|0\n") },
    };
    return table;
}
} // namespace

juce::StringArray KitMap::kontaktLibraries()
{
    juce::StringArray names;
    for (const auto& [lib, rows] : kontaktLibraryTable())
        names.add (lib.name);
    return names;
}

KitMap KitMap::kontaktLibrary (const juce::String& name)
{
    KitMap m;
    for (const auto& [lib, rows] : kontaktLibraryTable())
    {
        if (name != lib.name)
            continue;
        m.kitName = lib.name;
        for (const auto& line : juce::StringArray::fromLines (rows.trim()))
        {
            const auto f = juce::StringArray::fromTokens (line, "|", "");
            KitZone zone { f[0], f[1], kontaktNote (f[2]), -1, f[3] == "1" };
            zone.instrument = juce::String (lib.name) + "/" + f[0];
            zone.library = lib.library;
            m.zones.add (zone);
        }
    }
    return m;
}

juce::ValueTree KitMap::toValueTree() const
{
    juce::ValueTree tree ("KitMap");
    tree.setProperty ("kitName", kitName, nullptr);
    for (const auto& z : zones)
        tree.appendChild (juce::ValueTree ("Zone", { { "piece", z.piece }, { "label", z.label },
                                                     { "note", z.note }, { "cc4", z.cc4 },
                                                     { "enabled", z.enabled }, { "pluginName", z.pluginName },
                                                     { "hits", z.hits }, { "instrument", z.instrument },
                                                     { "name", z.name }, { "library", z.library },
                                                     { "image", z.image } }),
                          nullptr);
    return tree;
}

KitMap KitMap::fromValueTree (const juce::ValueTree& tree)
{
    KitMap m;
    m.kitName = tree["kitName"].toString();
    for (const auto& z : tree)
        m.zones.add ({ z["piece"].toString(), z["label"].toString(), (int) z["note"],
                       (int) z.getProperty ("cc4", -1), (bool) z.getProperty ("enabled", true),
                       z["pluginName"].toString() });
    for (int i = 0; i < m.zones.size(); ++i)
    {
        m.zones.getReference (i).hits = (int) tree.getChild (i).getProperty ("hits", 0);
        m.zones.getReference (i).instrument = tree.getChild (i)["instrument"].toString();
        m.zones.getReference (i).name = tree.getChild (i)["name"].toString();
        m.zones.getReference (i).library = tree.getChild (i)["library"].toString();
        if (const auto* image = tree.getChild (i)["image"].getBinaryData())
            m.zones.getReference (i).image = *image;
    }
    return m;
}
} // namespace emk
