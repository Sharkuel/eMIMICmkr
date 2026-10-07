#include "KitMap.h"

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
        { "Tom 1", "TOM1 CENTER", 48 },
        { "Tom 2", "TOM2 CENTER", 45 },
        { "Tom 3", "TOM3 CENTER", 43 },
        { "Tom 4", "TOM4 CENTER", 41 },
         })
        m.zones.add (zone);
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

juce::ValueTree KitMap::toValueTree() const
{
    juce::ValueTree tree ("KitMap");
    tree.setProperty ("kitName", kitName, nullptr);
    for (const auto& z : zones)
        tree.appendChild (juce::ValueTree ("Zone", { { "piece", z.piece }, { "label", z.label },
                                                     { "note", z.note }, { "cc4", z.cc4 },
                                                     { "enabled", z.enabled } }),
                          nullptr);
    return tree;
}

KitMap KitMap::fromValueTree (const juce::ValueTree& tree)
{
    KitMap m;
    m.kitName = tree["kitName"].toString();
    for (const auto& z : tree)
        m.zones.add ({ z["piece"].toString(), z["label"].toString(), (int) z["note"],
                       (int) z.getProperty ("cc4", -1), (bool) z.getProperty ("enabled", true) });
    return m;
}
} // namespace emk
