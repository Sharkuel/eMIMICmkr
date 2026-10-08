#include "KitMap.h"
#include <algorithm>
#include <map>
#include <set>

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

juce::String KitMap::toCsv() const
{
    juce::String out = "piece,zone,note,cc4,record,hits,instrument\n";
    for (const auto& z : zones)
        out << z.piece.replace (",", " ") << "," << z.label.replace (",", " ") << "," << z.note << ","
            << (z.cc4 >= 0 ? juce::String (z.cc4) : juce::String()) << "," << (z.enabled ? "yes" : "no") << ","
            << (z.hits > 0 ? juce::String (z.hits) : juce::String()) << "," << z.instrument.replace (",", " ") << "\n";
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
        m.zones.getReference (m.zones.size() - 1).instrument = cells[6];
    }
    if (m.zones.isEmpty())
        error = "No zones found.";
    return m;
}

void KitMap::applyNames (const std::map<juce::String, juce::String>& namesById)
{
    std::map<juce::String, juce::String> renamed;   // old piece -> new name
    juce::StringArray taken;
    for (const auto& z : zones)
        if (auto it = namesById.find (z.instrument); z.instrument.isNotEmpty() && it != namesById.end()
                                                     && renamed.count (z.piece) == 0)
        {
            auto name = it->second;
            for (int n = 2; taken.contains (name); ++n)
                name = it->second + " " + juce::String (n);
            taken.add (name);
            renamed[z.piece] = name;
        }
    for (auto& z : zones)
        if (auto it = renamed.find (z.piece); it != renamed.end())
            z.piece = it->second;
}

juce::String KitMap::sd3TextIn (const juce::MemoryBlock& data)
{
    auto find = [] (const juce::MemoryBlock& block) -> juce::String
    {
        const auto* bytes = static_cast<const char*> (block.getData());
        const auto size = block.getSize();
        static const std::string marker = "PlugVersion";
        const auto at = std::search (bytes, bytes + size, marker.begin(), marker.end());
        if (at == bytes + size)
            return {};
        const auto end = std::find (at, bytes + size, '\0');
        return juce::String::fromUTF8 (at, (int) (end - at));
    };
    if (auto text = find (data); text.isNotEmpty())
        return text;
    // Some hosts and plugins compress their state.
    juce::MemoryInputStream raw (data, false);
    juce::GZIPDecompressorInputStream unzipped (raw);
    juce::MemoryBlock inflated;
    juce::MemoryOutputStream out (inflated, false);
    out.writeFromInputStream (unzipped, 64 * 1024 * 1024);
    out.flush();
    return find (inflated);
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
            for (const auto& [pad, notes] : slot.pads)   // percussion and the like: listen and name them
                m.zones.add ({ slot.pos, "NOTE " + juce::String (notes[0]), notes[0], -1, false, slot.pos + ": " + pad });
            continue;
        }
        const int n = ++counts[family];
        const juce::String num (n), upper = juce::String (family).toUpperCase();
        const juce::String piece = juce::String (family) == "kick" ? "Kick"
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
        const juce::String labelHead = juce::String (family) == "kick" ? juce::String ("KICK1") : upper + num;
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
                m.zones.add ({ piece, labelHead + " " + code.toUpperCase(), notes[0], -1, false, slot.pos + ": " + code });
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
                                                     { "hits", z.hits }, { "instrument", z.instrument } }),
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
    }
    return m;
}
} // namespace emk
