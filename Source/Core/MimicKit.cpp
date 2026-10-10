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

mci::MciFile build (const mci::MciFile& templateKit, const std::vector<Instrument>& instruments, Report& report)
{
    const auto slots = slotsOf (templateKit);
    if (slots.isEmpty())
        throw mci::Error ("the template is not a Mimic kit (it has no slots)");

    // A slot that was empty in the template has no mixer channel; it gets a copy of the first
    // loaded slot's channel, with its EQ and compressor off.
    juce::String donor;
    for (const auto& s : slots)
        if (loaded (templateKit, s))
        {
            donor = s;
            break;
        }

    const auto placed = assignSlots (slots, instruments, templateKit, report);
    auto kit = templateKit;
    for (const auto& slot : slots)
    {
        auto old = channelKeys (kit, slot);
        for (const auto& k : old)
            kit.meta.erase (k);

        const auto at = placed.find (slot);
        if (at == placed.end())
        {
            kit.set (key (slot, "lsin"), Value::ofString ({}));
            kit.set (key (slot, "lsln"), Value::ofString ({}));
            kit.set (key (slot, "tnm"), Value::ofString ({}));
            kit.set (key (slot, "isld"), Value::ofU8 (0));
            continue;
        }

        const auto& in = instruments[at->second];
        const auto& info = types().count (in.type()) ? types().at (in.type()) : types().at ("Wave");
        report.placed.emplace_back (slot, in.name());

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

        // The channel: the template's own (or the donor's), its mics replaced by the instrument's.
        auto source = slot;
        if (old.empty() || templateKit.getInt (key (slot, "isld")) == 0)
            source = donor;
        if (source.isEmpty())
            throw mci::Error ("the template kit has no instrument loaded to copy mixer settings from");
        float level = 0.0f;
        for (const auto& k : channelKeys (templateKit, source))
        {
            const auto rest = k.substr (3 + (size_t) source.getNumBytesAsUTF8());
            if (rest.rfind ("mi", 0) == 0)
            {
                if (rest.size() > 4 && rest.compare (rest.size() - 4, 4, "micv") == 0)
                    level = std::max (level, templateKit.meta.at (k).f);
                continue;
            }
            auto v = templateKit.meta.at (k);
            if (source != slot && (std::regex_match (rest, std::regex ("cmp[0-7]en")) || std::regex_match (rest, std::regex ("eq[0-7]e5v1en"))))
                v.i = 0;
            kit.set (key (slot, rest.c_str(), "CME"), v);
        }
        if (level <= 0.0f)
            level = 1.0f;

        const auto mics = std::max (1, in.settings.getInt ("INST0micnt", 1));
        kit.set (key (slot, "nm", "CME"), Value::ofInt (mics));
        for (int m = 0; m < mics; ++m)
        {
            const auto from = "INST0micinf" + std::to_string (m);
            const auto to = "mi" + std::to_string (m);
            for (auto* k : micKeys)
                if (const auto* v = in.settings.find (from + k))
                    kit.set (key (slot, (to + k).c_str(), "CME"), *v);
            kit.set (key (slot, (to + "micp").c_str(), "CME"), Value::ofFloat (0.5));   // centred
            kit.set (key (slot, (to + "micv").c_str(), "CME"),
                     Value::ofFloat (in.settings.getFloat (from + "micv", 1.0f) * level));
        }
    }
    return kit;
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
