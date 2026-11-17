#include "MimicKit.h"
#include <regex>
#include <set>

namespace emk::MimicKit
{
namespace
{
using mci::Value;

struct TypeInfo
{
    std::vector<const char*> articulations;
    std::vector<int> zoneTypes;
};

/** What a slot holding each instrument type lists; the same in every kit Pearl ships. */
const std::map<juce::String, TypeInfo>& types()
{
    static const std::map<juce::String, TypeInfo> t {
        { "Kick",   { { "Center" }, { 1 } } },
        { "Snare",  { { "Center", "Side", "Rim Click", "Side Stick", "Rim Shot", "Rim Shot Side" }, { 1, 6, 2, 4, 3, 5 } } },
        { "Tom",    { { "Center", "Rim Click", "Rim Shot" }, { 1, 2, 3 } } },
        { "Hat",    { { "Bow Single Zone", "Bow Dual Zone", "Edge Dual Zone", "Chick", "Foot Splash" }, { 80, 81, 82, 83, 84 } } },
        { "Ride",   { { "Bow", "Bell", "Crash", "Mute" }, { 40, 42, 43, 41 } } },
        { "Cymbal", { { "Bow", "Crash", "Mute" }, { 40, 43, 41 } } },
        { "Perc",   { { "Main", "Secondary", "Additional 1", "Additional 2" }, { 200, 201, 202, 203 } } },
        { "Wave",   { { "Center" }, { 1 } } },
    };
    return t;
}

/** What follows "CME<slot>" in a slot's mixer channel keys. Bus channels ("CMEKicks...",
    "CMERoom...") never match one of these after a slot name. */
const std::regex& channelSuffix()
{
    static const std::regex r ("(cb|cv|dov|iv|mal|nm|sd|sv|sva|sw|tn|vr"
                               "|cmp[0-7](at|clen|en|ot|pc|pn|rl|rt|scen|scf|th)"
                               "|eq[0-7]e5v1(en)?"
                               "|mi[0-9]+(att|enven|isst|micen|micm|micn|micp|micpos|micpz|micrs|mict|mictn|micv|rel|sus))");
    return r;
}

const char* const micKeys[] { "att", "enven", "isst", "micen", "micm", "micn", "micp", "micpos",
                              "micpz", "micrs", "mict", "mictn", "micv", "rel", "sus" };

std::string key (const juce::String& slot, const char* what, const char* prefix = "insstCS")
{
    return prefix + slot.toStdString() + what;
}

std::vector<std::string> channelKeys (const mci::MciFile& kit, const juce::String& slot)
{
    const auto prefix = "CME" + slot.toStdString();
    std::vector<std::string> keys;
    for (auto it = kit.meta.lower_bound (prefix); it != kit.meta.end() && it->first.compare (0, prefix.size(), prefix) == 0; ++it)
        if (std::regex_match (it->first.substr (prefix.size()), channelSuffix()))
            keys.push_back (it->first);
    return keys;
}

bool loaded (const mci::MciFile& kit, const juce::String& slot)
{
    return kit.getInt (key (slot, "isld")) != 0 && ! channelKeys (kit, slot).empty();
}

/** The first number in a name ("10x7 BRITISH DRUM COMPANY Legend" -> 10), or a large one. */
int sizeOf (const juce::String& name)
{
    const auto at = name.indexOfAnyOf ("0123456789");
    return at < 0 ? 1000 : name.substring (at).getIntValue();
}

bool isCrash (const Instrument& i)
{
    return i.name().containsIgnoreCase ("crash");
}
} // namespace

Instrument readInstrument (const juce::File& file)
{
    Instrument i { file, mci::readSettings (file) };
    if (i.name().isEmpty())
        throw mci::Error (file.getFileName().toStdString() + " is not a Mimic instrument");
    return i;
}

juce::StringArray slotsOf (const mci::MciFile& kit)
{
    std::vector<std::pair<int, juce::String>> found;
    for (const auto& [k, v] : kit.meta)
        if (k.rfind ("insstCS", 0) == 0 && k.size() > 12 && k.compare (k.size() - 5, 5, "isltn") == 0)
            found.emplace_back (v.i, juce::String (k.substr (7, k.size() - 12)));
    std::sort (found.begin(), found.end());
    juce::StringArray slots;
    for (const auto& s : found)
        slots.add (s.second);
    return slots;
}

std::map<juce::String, size_t> assignSlots (const juce::StringArray& slots, const std::vector<Instrument>& instruments,
                                            const mci::MciFile& kit, Report& report)
{
    std::map<juce::String, size_t> placed;
    auto usable = [&] (const juce::String& slot)
    {
        return slots.contains (slot) && placed.count (slot) == 0 && kit.getInt (key (slot, "enb"), 1) != 0;
    };
    juce::StringArray spare;   // Aux 1-5 then Xtra 1-8
    for (const auto& s : slots)
        if (s.startsWith ("Aux ") || s.startsWith ("Xtra "))
            spare.add (s);

    auto place = [&] (size_t i, juce::StringArray wanted)
    {
        wanted.addArray (spare);
        for (const auto& slot : wanted)
            if (usable (slot))
            {
                placed[slot] = i;
                return slot;
            }
        report.leftOut.add (instruments[i].name());
        return juce::String();
    };

    std::vector<size_t> kicks, snares, hats, rides, toms, cymbals, others;
    std::set<std::pair<juce::String, juce::String>> names;
    for (size_t i = 0; i < instruments.size(); ++i)
    {
        const auto& in = instruments[i];
        // The Mimic finds a slot's instrument by name and library, so two with the same pair
        // would both load the same one.
        if (! names.emplace (in.name(), in.settings.getString ("INST0libnam")).second)
        {
            report.leftOut.add (in.name());
            report.notes.add (in.file.getFileName() + " has the same name as another instrument; rename one so the Mimic can tell them apart.");
            continue;
        }
        const auto type = in.type();
        if (type == "Kick") kicks.push_back (i);
        else if (type == "Snare") snares.push_back (i);
        else if (type == "Hat") hats.push_back (i);
        else if (type == "Ride" && ! isCrash (in)) rides.push_back (i);
        else if (type == "Tom") toms.push_back (i);
        else if (type == "Cymbal" || type == "Ride") cymbals.push_back (i);
        else others.push_back (i);
    }
    auto bySize = [&] (size_t a, size_t b) { return sizeOf (instruments[a].name()) < sizeOf (instruments[b].name()); };
    std::stable_sort (toms.begin(), toms.end(), bySize);
    std::stable_sort (cymbals.begin(), cymbals.end(), [&] (size_t a, size_t b)
                      { return isCrash (instruments[a]) && ! isCrash (instruments[b]); });

    for (auto i : kicks) place (i, { "Kick" });
    for (auto i : snares) place (i, { "Snare" });
    for (size_t n = 0; n < hats.size(); ++n)
    {
        if (n == 0)
            place (hats[n], { "Hi-Hat" });
        else
            report.leftOut.add (instruments[hats[n]].name());   // a kit has one hi-hat
    }
    for (auto i : rides) place (i, { "Ride 1", "Ride 2" });
    for (auto i : toms) place (i, { "Tom 1", "Tom 2", "Tom 3", "Tom 4" });
    // The Mimic plays a ride-type instrument from any cymbal input (Alexandre, 2026-10-10).
    for (auto i : cymbals) place (i, { "Crash 1", "Crash 2" });
    for (auto i : others) place (i, {});
    return placed;
}

namespace
{
/** The first slot with an instrument loaded: an empty slot copies its mixer channel. */
juce::String donorOf (const mci::MciFile& kit)
{
    for (const auto& s : slotsOf (kit))
        if (loaded (kit, s))
            return s;
    return {};
}

void emptySlot (mci::MciFile& kit, const juce::String& slot)
{
    for (const auto& k : channelKeys (kit, slot))
        kit.meta.erase (k);
    kit.set (key (slot, "lsin"), Value::ofString ({}));
    kit.set (key (slot, "lsln"), Value::ofString ({}));
    kit.set (key (slot, "tnm"), Value::ofString ({}));
    kit.set (key (slot, "isld"), Value::ofU8 (0));
}

/** Puts `in` in `slot`. The mixer channel comes from `source` (a slot of `from`): its EQ,
    compressors and levels, with the instrument's own mics. A channel copied from another slot
    has its EQ and compressors off. */
void fill (mci::MciFile& kit, const juce::String& slot, const Instrument& in,
           const mci::MciFile& from, const juce::String& source, bool centre)
{
    if (source.isEmpty())
        throw mci::Error ("the kit has no instrument loaded to copy mixer settings from");
    const auto channel = channelKeys (from, source);   // before emptying, `from` may be `kit`
    std::vector<std::pair<std::string, Value>> copied;
    std::map<int, float> pans;   // the old mics' pans, kept unless centring
    float level = 0.0f;
    for (const auto& k : channel)
    {
        const auto rest = k.substr (3 + (size_t) source.getNumBytesAsUTF8());
        if (rest.rfind ("mi", 0) == 0)
        {
            if (rest.size() > 4 && rest.compare (rest.size() - 4, 4, "micv") == 0)
                level = std::max (level, from.meta.at (k).f);
            if (source == slot && rest.size() > 4 && rest.compare (rest.size() - 4, 4, "micp") == 0)
                pans[std::atoi (rest.c_str() + 2)] = from.meta.at (k).f;
            continue;
        }
        auto v = from.meta.at (k);
        if (source != slot && (std::regex_match (rest, std::regex ("cmp[0-7]en")) || std::regex_match (rest, std::regex ("eq[0-7]e5v1en"))))
            v.i = 0;
        copied.emplace_back (rest, v);
    }
    if (level <= 0.0f)
        level = 1.0f;

    for (const auto& k : channelKeys (kit, slot))
        kit.meta.erase (k);
    for (const auto& [rest, v] : copied)
        kit.set (key (slot, rest.c_str(), "CME"), v);

    const auto& info = types().count (in.type()) ? types().at (in.type()) : types().at ("Wave");
    for (auto it = kit.meta.begin(); it != kit.meta.end();)
        it = it->first.rfind (key (slot, "artn"), 0) == 0 ? kit.meta.erase (it) : std::next (it);
    kit.set (key (slot, "itp"), Value::ofString (types().count (in.type()) ? in.type() : juce::String ("Wave")));
    kit.set (key (slot, "artc"), Value::ofInt ((int) info.articulations.size()));
    for (size_t a = 0; a < info.articulations.size(); ++a)
        kit.set (key (slot, ("artn" + std::to_string (a)).c_str()), Value::ofString (info.articulations[a]));
    kit.set (key (slot, "azt"), Value::ofInts (info.zoneTypes));
    kit.set (key (slot, "enb"), Value::ofU8 (1));
    kit.set (key (slot, "isld"), Value::ofU8 (1));
    kit.set (key (slot, "lsin"), Value::ofString (in.name()));
    kit.set (key (slot, "lsln"), Value::ofString (in.settings.getString ("INST0libnam")));
    kit.set (key (slot, "tnm"), Value::ofString (kit.getString (key (slot, "nm"))));

    const auto mics = std::max (1, in.settings.getInt ("INST0micnt", 1));
    kit.set (key (slot, "nm", "CME"), Value::ofInt (mics));
    for (int m = 0; m < mics; ++m)
    {
        const auto fromMic = "INST0micinf" + std::to_string (m);
        const auto to = "mi" + std::to_string (m);
        for (auto* k : micKeys)
            if (const auto* v = in.settings.find (fromMic + k))
                kit.set (key (slot, (to + k).c_str(), "CME"), *v);
        kit.set (key (slot, (to + "micp").c_str(), "CME"),
                 Value::ofFloat (centre || pans.count (m) == 0 ? 0.5f : pans[m]));
        kit.set (key (slot, (to + "micv").c_str(), "CME"),
                 Value::ofFloat (in.settings.getFloat (fromMic + "micv", 1.0f) * level));
    }
}
} // namespace

mci::MciFile build (const mci::MciFile& templateKit, const std::vector<Instrument>& instruments, Report& report)
{
    const auto slots = slotsOf (templateKit);
    if (slots.isEmpty())
        throw mci::Error ("the template is not a Mimic kit (it has no slots)");

    const auto donor = donorOf (templateKit);
    const auto placed = assignSlots (slots, instruments, templateKit, report);
    auto kit = templateKit;
    for (const auto& slot : slots)
    {
        const auto at = placed.find (slot);
        if (at == placed.end())
        {
            emptySlot (kit, slot);
            continue;
        }
        const auto& in = instruments[at->second];
        report.placed.emplace_back (slot, in.name());
        // The template's own channel for the slot, or the donor's.
        const auto source = loaded (templateKit, slot) ? slot : donor;
        fill (kit, slot, in, templateKit, source, true);
    }
    return kit;
}

std::vector<Slot> slotList (const mci::MciFile& kit)
{
    std::vector<Slot> list;
    for (const auto& s : slotsOf (kit))
    {
        Slot slot;
        slot.name = s;
        slot.loaded = loaded (kit, s);
        slot.enabled = kit.getInt (key (s, "enb"), 1) != 0;
        if (slot.loaded)
        {
            slot.instrument = kit.getString (key (s, "lsin"));
            slot.library = kit.getString (key (s, "lsln"));
            slot.type = kit.getString (key (s, "itp"));
        }
        list.push_back (slot);
    }
    return list;
}

void place (mci::MciFile& kit, const juce::String& slot, const Instrument& in)
{
    if (! slotsOf (kit).contains (slot))
        throw mci::Error ("the kit has no slot " + slot.toStdString());
    // A slot already holding something keeps its own mixer channel; an empty one borrows one.
    const auto source = loaded (kit, slot) ? slot : donorOf (kit);
    fill (kit, slot, in, kit, source, false);
}

void empty (mci::MciFile& kit, const juce::String& slot)
{
    emptySlot (kit, slot);
}

bool canTake (const juce::String& slot, const Instrument& in, juce::String& why)
{
    const bool hat = in.type() == "Hat";
    if (hat != (slot == "Hi-Hat"))
    {
        why = hat ? "A hi-hat only goes in the Hi-Hat slot." : "Only a hi-hat goes in the Hi-Hat slot.";
        return false;
    }
    return true;
}

void setEnabled (mci::MciFile& kit, const juce::String& slot, bool on)
{
    kit.set (key (slot, "enb"), Value::ofU8 (on ? 1 : 0));
}

std::vector<Mic> mics (const mci::MciFile& kit, const juce::String& slot)
{
    std::vector<Mic> list;
    if (! loaded (kit, slot))
        return list;
    const auto n = kit.getInt (key (slot, "nm", "CME"));
    for (int m = 0; m < n; ++m)
    {
        const auto M = "mi" + std::to_string (m);
        Mic mic;
        mic.name = kit.getString (key (slot, (M + "micn").c_str(), "CME")).trim();
        mic.level = kit.getFloat (key (slot, (M + "micv").c_str(), "CME"), 1.0f);
        mic.pan = kit.getFloat (key (slot, (M + "micp").c_str(), "CME"), 0.5f);
        mic.muted = kit.getInt (key (slot, (M + "micm").c_str(), "CME")) != 0;
        list.push_back (mic);
    }
    return list;
}

void setMic (mci::MciFile& kit, const juce::String& slot, int index, const Mic& mic)
{
    const auto M = "mi" + std::to_string (index);
    kit.set (key (slot, (M + "micv").c_str(), "CME"), Value::ofFloat (mic.level));
    kit.set (key (slot, (M + "micp").c_str(), "CME"), Value::ofFloat (mic.pan));
    // Keep the u8/i32 section the kit already uses for the mute.
    auto muted = kit.find (key (slot, (M + "micm").c_str(), "CME")) != nullptr ? *kit.find (key (slot, (M + "micm").c_str(), "CME"))
                                                                                  : Value::ofU8 (0);
    muted.i = mic.muted ? 1 : 0;
    kit.set (key (slot, (M + "micm").c_str(), "CME"), muted);
}

float channelValue (const mci::MciFile& kit, const juce::String& slot, const char* what, float fallback)
{
    return kit.getFloat (key (slot, what, "CME"), fallback);
}

void setChannelValue (mci::MciFile& kit, const juce::String& slot, const char* what, float value)
{
    if (loaded (kit, slot))
        kit.set (key (slot, what, "CME"), Value::ofFloat (value));
}

int centreAll (mci::MciFile& kit)
{
    int changed = 0;
    for (const auto& s : slotsOf (kit))
    {
        auto list = mics (kit, s);
        for (size_t m = 0; m < list.size(); ++m)
            if (list[m].pan != 0.5f)
            {
                list[m].pan = 0.5f;
                setMic (kit, s, (int) m, list[m]);
                ++changed;
            }
    }
    return changed;
}

mci::MciFile emptied (const mci::MciFile& templateKit)
{
    if (slotsOf (templateKit).isEmpty())
        throw mci::Error ("the template is not a Mimic kit (it has no slots)");
    Report report;
    return build (templateKit, {}, report);
}

juce::String kitName (const juce::File& file)
{
    auto name = file.getFileNameWithoutExtension();
    if (name.startsWith ("[U] "))
        name = name.substring (4);
    else if (name.startsWith ("_U__") || name.startsWith ("U-"))
        name = name.fromFirstOccurrenceOf (name.startsWith ("U-") ? "U-" : "_U__", false, false);
    return name;
}

juce::File kitFolderOf (const juce::File& file)
{
    const auto parent = file.getParentDirectory();
    return parent.getFileExtension().equalsIgnoreCase (".mdp") ? parent.getParentDirectory() : parent;
}

juce::File kitFile (const juce::File& folder, const juce::String& kitName)
{
    const auto name = juce::File::createLegalFileName (kitName.trim());
    return folder.getChildFile (name + ".mdp").getChildFile ("[U] " + name + ".kit");
}

Report buildFile (const juce::Array<juce::File>& mciFiles, const juce::File& templateKit, const juce::File& out)
{
    std::vector<Instrument> instruments;
    for (const auto& f : mciFiles)
        instruments.push_back (readInstrument (f));
    std::stable_sort (instruments.begin(), instruments.end(),
                      [] (const Instrument& a, const Instrument& b) { return a.file.getFileName() < b.file.getFileName(); });

    Report report;
    const auto kit = build (mci::readSettings (templateKit), instruments, report);
    if (! out.getParentDirectory().createDirectory())
        throw mci::Error ("cannot create " + out.getParentDirectory().getFullPathName().toStdString());
    const auto result = mci::writeKitFile (kit, out);
    if (result.failed())
        throw mci::Error (result.getErrorMessage().toStdString());
    return report;
}
} // namespace emk::MimicKit
