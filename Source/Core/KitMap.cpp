#include "KitMap.h"
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <functional>

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
int cellsFor (const KitMap& map, const KitZone& zone)
{
    const auto family = familyOf (zone.label);
    bool hasBell = false;
    for (const auto& z : map.zones)
        if (z.enabled && z.piece == zone.piece && z.label.contains ("BELL"))
            hasBell = true;
    const bool cymbal = family == "CRASH" || family == "SPLASH" || family == "CHINA" || family == "STACK"
                        || family == "CYMBAL";
    return family == "KICK" ? 64
         : family == "SNARE" ? 300
         : family == "TOM" ? 200
         : family == "RIDE" || (cymbal && hasBell) ? 299
         : cymbal ? 199
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
        return juce::jlimit (1, juce::jmin (maxHits, cellsFor (*this, zone)), zone.hits);

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
    return juce::jlimit (1, maxHits, (cellsFor (*this, zone) - taken) / juce::jmax (1, sharing));
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
    return zone.hits = juce::jlimit (1, juce::jmax (1, cellsFor (*this, zone) - othersTaken), count);
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
        if (n.contains ("crash") || n.contains ("cymbal")) return "crash";
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
            for (const auto& [artic, artName] : artics[slot])   // anything else: there to listen to and tick
                if (used.count (artic) == 0)
                    add (piece, head + " " + artName.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "), artic, false);
            if (family != "kick" && family != "snare" && family != "tom")
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
