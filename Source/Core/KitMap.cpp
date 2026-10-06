#include "KitMap.h"

namespace emk
{
KitMap KitMap::generalMidi()
{
    // Zone type ids come from Mimic Instrument Editor files; see docs/mci-format.md.
    KitMap m;
    for (const auto& zone : std::initializer_list<KitZone> {
        { "Kick", "Kick", 36, 1 },
        { "Snare", "Center", 38, 1 },
        { "Snare", "Rimshot", 40, 3 },
        { "Snare", "Sidestick", 37, 4 },
        { "Tom 1", "Center", 48, 1 },
        { "Tom 2", "Center", 45, 1 },
        { "Floor Tom", "Center", 43, 1 },
        { "Hi-Hat", "Closed", 42, 0 },
        { "Hi-Hat", "Pedal", 44, 0 },
        { "Hi-Hat", "Open", 46, 0 },
        { "Crash 1", "Edge", 49, 0 },
        { "Crash 2", "Edge", 57, 0 },
        { "Ride", "Bow", 51, 40 },
        { "Ride", "Bell", 53, 42 },
        { "Ride", "Edge", 59, 43 },
        { "China", "Edge", 52, 0 },
        { "Splash", "Edge", 55, 0 },
         })
        m.zones.add (zone);
    return m;
}
} // namespace emk
