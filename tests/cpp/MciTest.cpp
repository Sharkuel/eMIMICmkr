// Checks of the C++ .mci writer and WAV converter, and the converter's command line.
//
//   eMIMICmkr_mci_tests                      run the self-checks
//   eMIMICmkr_mci_tests --convert INPUT [INPUT...] [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]
//                                            convert a folder or .zip of rendered WAVs, or
//                                            several folders read as one
//   eMIMICmkr_mci_tests --rewrite IN OUT     read a .mci and write it back (reader/writer check)
//   eMIMICmkr_mci_tests --rewrite-kit IN OUT read a .kit and write it back
//   eMIMICmkr_mci_tests --kit TEMPLATE OUT MCI...
//                                            build a kit preset from instruments
//
// tests/test_cpp_converter.py compares --convert with tools/mci/wav2mci.py.
#include "../../Source/Core/MimicKit.h"
#include "../../Source/Core/WavToMci.h"
#include <cstdio>
#include <random>

namespace
{
using namespace emk;

int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("FAILED: %s\n", what);
    }
}

void checkCodec()
{
    std::mt19937 rng (7);
    std::uniform_int_distribution<int> full (-(1 << 23), (1 << 23) - 1), small (-300, 300);
    for (size_t length : { 0u, 1u, 2u, 3u, 1016u, 1017u, 8129u, 50000u })
    {
        mci::Samples x (length);
        for (size_t i = 0; i < length; ++i)
            x[i] = (i / 997) % 2 == 0 ? full (rng) : small (rng) * 256;   // wide and narrow stretches
        juce::MemoryOutputStream out;
        const auto frames = mci::encodeBlocks (x, out);
        check (out.getDataSize() == frames.size() * (size_t) mci::blockSize, "blocks are 1024 bytes");
        check (mci::decodeBlocks (out.getData(), out.getDataSize()) == x, "codec round trip");
    }
    const mci::Samples extremes { (1 << 23) - 1, -(1 << 23), (1 << 23) - 1, 0, -(1 << 23) };
    juce::MemoryOutputStream out;
    mci::encodeBlocks (extremes, out);
    check (mci::decodeBlocks (out.getData(), out.getDataSize()) == extremes, "codec handles full-scale jumps");

    check (mci::headLayout ({}).desc == mci::headPad, "empty head");
    const auto layout = mci::headLayout (std::vector<int> (100, 1000));
    check (layout.nobu == 32 && layout.desc == 63 + 32000 + 1, "head layout");
}

void checkBuilders()
{
    using Shares = WavToMci::Shares;
    check (WavToMci::shareCells (199, { { "edge", 127 }, { "bow", 127 } }) == Shares { { "edge", 100 }, { "bow", 99 } },
           "shareCells splits evenly");
    check (WavToMci::shareCells (300, { { "center", 200 }, { "rimshot", 200 }, { "sidestick", 40 } })
               == Shares { { "sidestick", 40 }, { "center", 130 }, { "rimshot", 130 } },
           "shareCells hands spare cells on");

    WavToMci::Hits hits;
    for (int v = 1; v <= 100; ++v)
        hits.push_back ({ v, {}, {} });
    const auto layers = WavToMci::splitLayers (hits);
    check (layers.size() == 20 && layers[0].size() == 5, "splitLayers");
    const auto vel = WavToMci::minVelocities (layers);
    check (vel[0] == 0 && vel[1] == 6 && vel[2] == 11, "minVelocities");

    auto hitsOf = [] (int n)
    {
        WavToMci::Hits h;
        for (int i = 0; i < n; ++i)
            h.push_back ({ 1 + i * 126 / std::max (1, n - 1), {}, {} });
        return h;
    };
    auto sizes = [] (const std::vector<WavToMci::Hits>& split)
    {
        std::vector<size_t> out;
        for (const auto& l : split)
            out.push_back (l.size());
        return out;
    };
    check (WavToMci::roundRobins ("kick", "kick") == 4 && WavToMci::roundRobins ("snare", "rimshot") == 6
               && WavToMci::roundRobins ("tom", "center") == 5 && WavToMci::roundRobins ("snare", "sidestick") == 4
               && WavToMci::roundRobins ("cymbal", "edge") == 4,
           "roundRobins");
    check (sizes (WavToMci::roundRobinLayers (hitsOf (64), 4)) == std::vector<size_t> (16, 4), "64 kick hits: 16 layers of 4");
    check (WavToMci::roundRobinLayers (hitsOf (100), 6).size() == 17, "100 snare hits: 17 layers");
    check (sizes (WavToMci::roundRobinLayers (hitsOf (300), 4)) == std::vector<size_t> (20, 6), "300 cymbal hits: 20 layers of 6");
    check (sizes (WavToMci::roundRobinLayers (hitsOf (3), 4)) == std::vector<size_t> { 3 }, "3 sidesticks: 1 layer");

    check (WavToMci::pickOpenness ({ "tight", "closed2", "open1", "open2", "open3", "open4", "open5", "open6" })
               == juce::StringArray { "closed2", "open2", "open3", "open4", "open6" },
           "pickOpenness spreads closed to open");
    check (WavToMci::pickOpenness ({ "closed", "open1" }) == juce::StringArray { "closed", "open1", "open1", "open1", "open1" },
           "pickOpenness repeats the most open");
    check (WavToMci::classify ("RIDE1 BELL") == std::pair<juce::String, juce::String> ("cymbal", "bell"), "classify ride bell");
    check (WavToMci::classify ("HATS BOW OPEN3") == std::pair<juce::String, juce::String> ("hat", "bow:open3"), "classify hat");
    check (WavToMci::classify ("HATS FOOT SPLASH").second == "footsplash", "classify foot splash");
    check (WavToMci::classify ("TOM2 RIM").second == "rimshot", "classify tom rim");
    check (WavToMci::classify ("SNARE1 RIMSHOT SHALLOW").second == "rimshotside"
           && WavToMci::classify ("SNARE1 RIMSHOT SIDE").second == "rimshotside"
           && WavToMci::classify ("TOM1 RIMSHOT SHALLOW").second == "rimshot", "classify shallow rimshots");

    check (mci::velocityThresholds ({ 1.0, 4.0, 16.0 }) == std::vector<int> { 0, 64, 127 }, "velocityThresholds");

    // A loud 30 ms click on top of a quieter ring: the choke keeps 3 s of the ring only.
    const int rate = 44100;
    std::vector<double> hit ((size_t) rate * 5);
    for (size_t i = 0; i < hit.size(); ++i)
        hit[i] = ((double) i < rate * 0.03 ? 1.0 : 0.2) * std::sin ((double) i / 3.0);
    const auto choke = mci::makeChoke (hit, rate);
    double loudest = 0.0;
    for (auto v : choke)
        loudest = std::max (loudest, std::abs (v));
    check (choke.size() == (size_t) (3 * rate) && loudest <= 0.2 + 1e-9 && std::abs (choke.front()) < 1e-12
               && std::abs (choke.back()) < 1e-3,
           "makeChoke keeps only the ring");
}

void checkContainer()
{
    mci::Samples s (5000);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (int) (std::sin ((double) i / 9.0) * 3000.0) * 256;
    auto arts = mci::cymbalArticulations ({ { s, s }, { s } }, { { s } }, { { s } });
    const auto f = mci::buildInstrument ("Test Crash", "Cymbal", arts, WavToMci::thumbnail(), {}, "Cymbal");
    const auto bytes = mci::writeMci (f);
    const auto again = mci::readMci (bytes.getData(), bytes.getSize());
    check (mci::writeMci (again) == bytes, "container round trip");
    check (again.getString ("INST0artic2artn") == "Choke Ring" && again.getInt ("INST0pool0psz") == 5, "metadata read back");
    for (int i = 0; i < again.getInt ("INST0pool0psz"); ++i)
        check (mci::sample (again, i) == s, "audio read back");

    juce::TemporaryFile temp (".mci");
    check (mci::writeMciFile (f, temp.getFile()).wasOk(), "writeMciFile");
    juce::MemoryBlock onDisk;
    temp.getFile().loadFileAsData (onDisk);
    check (onDisk == bytes, "writeMciFile writes what writeMci returns");

    auto broken = bytes;
    static_cast<char*> (broken.getData())[100] ^= 1;
    bool threw = false;
    try
    {
        mci::readMci (broken.getData(), broken.getSize());
    }
    catch (const mci::Error&)
    {
        threw = true;
    }
    check (threw, "a damaged file fails its MD5 check");
}

/** A made-up kit in the Mimic's layout: Kick and Kick Layer loaded, everything else empty. */
mci::MciFile templateKit()
{
    using mci::Value;
    const juce::StringArray slots { "Kick", "Kick Layer", "Snare", "Snare Layer", "Hi-Hat", "Ride 1", "Ride 2",
                                    "Tom 1", "Tom 2", "Tom 3", "Tom 4", "Crash 1", "Crash 2", "Aux 1", "Aux 2",
                                    "Aux 3", "Aux 4", "Aux 5", "Xtra 1", "Xtra 2", "Xtra 3", "Xtra 4", "Xtra 5",
                                    "Xtra 6", "Xtra 7", "Xtra 8" };
    mci::MciFile kit;
    kit.set ("KitVol", Value::ofFloat (1.0));
    kit.set ("TPADKICKKICKfprn", Value::ofString ("R-KD-120"));
    kit.set ("CMEKickscb", Value::ofFloat (0.7));   // the Kicks bus, not the Kick slot
    kit.set ("CMEKicksvr", Value::ofFloat (1.0));
    for (int n = 0; n < slots.size(); ++n)
    {
        const auto P = "insstCS" + slots[n].toStdString();
        const bool full = n < 2;
        kit.set (P + "isltn", Value::ofInt (n));
        kit.set (P + "itp", Value::ofString (full ? "Kick" : "Wave"));
        kit.set (P + "artc", Value::ofInt (1));
        kit.set (P + "artn0", Value::ofString ("Center"));
        kit.set (P + "azt", Value::ofInts ({ 1 }));
        kit.set (P + "enb", Value::ofU8 (slots[n] == "Ride 2" ? 0 : 1));
        kit.set (P + "isld", Value::ofU8 (full ? 1 : 0));
        kit.set (P + "lsin", Value::ofString (full ? "Factory Kick" : ""));
        kit.set (P + "lsln", Value::ofString (full ? "SSD 5" : ""));
        kit.set (P + "nm", Value::ofString (slots[n]));
        kit.set (P + "tnm", Value::ofString (full ? slots[n] : ""));
        kit.set (P + "mdir", Value::ofInt (16));
        if (! full)
            continue;
        const auto C = "CME" + slots[n].toStdString();
        kit.set (C + "cv", Value::ofFloat (1.0));
        kit.set (C + "nm", Value::ofInt (2));
        kit.set (C + "cmp0en", Value::ofU8 (1));
        kit.set (C + "eq0e5v1en", Value::ofU8 (1));
        for (int m = 0; m < 2; ++m)
        {
            const auto M = C + "mi" + std::to_string (m);
            kit.set (M + "micn", Value::ofString (m == 0 ? "Kick In" : "OH"));
            kit.set (M + "micp", Value::ofFloat (m == 0 ? 0.5 : 0.2));
            kit.set (M + "micv", Value::ofFloat (m == 0 ? 0.8 : 0.4));
        }
    }
    return kit;
}

void checkKits()
{
    const auto bytes = mci::writeKit (templateKit());
    const auto read = mci::readSettings (bytes.getData(), bytes.getSize());
    check (mci::writeKit (read) == bytes && read.meta.size() == templateKit().meta.size(), "kit round trip");
    check (MimicKit::slotsOf (read).size() == 26 && MimicKit::slotsOf (read)[4] == "Hi-Hat", "kit slots in order");

    juce::TemporaryFile dir;
    dir.getFile().createDirectory();
    mci::Samples s (3000, 1 << 16);
    const auto image = WavToMci::thumbnail();
    juce::Array<juce::File> files;
    auto add = [&] (const juce::String& name, const juce::String& type, std::vector<mci::Articulation> arts)
    {
        const auto file = dir.getFile().getChildFile (name + ".mci");
        check (mci::writeMciFile (mci::buildInstrument (name, type, arts, image), file).wasOk(), "write instrument");
        files.add (file);
    };
    const mci::Layers one { { s } };
    const auto drum = [&] { mci::Articulation a; a.name = "Center"; a.zoneType = 1; a.layers = one; return std::vector<mci::Articulation> { a }; };
    add ("22 Kick", "Kick", drum());
    add ("14 Snare", "Snare", drum());
    add ("12 Tom", "Tom", drum());
    add ("10 Tom", "Tom", drum());
    add ("16 Floor Tom", "Tom", drum());
    add ("20 Ride", "Ride", mci::rideArticulations (one, one, one));
    add ("16 Crash 3-zone", "Ride", mci::rideArticulations (one, one, one));
    add ("18 Crash", "Cymbal", mci::cymbalArticulations (one, one));
    add ("10 Splash", "Cymbal", mci::cymbalArticulations (one, one));

    const auto templateFile = dir.getFile().getChildFile ("template.kit");
    check (mci::writeKitFile (templateKit(), templateFile).wasOk(), "write template");
    const auto out = MimicKit::kitFile (dir.getFile(), "My Kit");
    check (out.getFileName() == "[U] My Kit.kit" && out.getParentDirectory().getFileName() == "My Kit.mdp", "kit file name");
    const auto report = MimicKit::buildFile (files, templateFile, out);
    const auto kit = mci::readSettings (out);

    auto in = [&] (const char* slot) { return kit.getString (std::string ("insstCS") + slot + "lsin"); };
    check (in ("Kick") == "22 Kick" && in ("Snare") == "14 Snare" && in ("Ride 1") == "20 Ride", "kick, snare, ride");
    check (in ("Tom 1") == "10 Tom" && in ("Tom 2") == "12 Tom" && in ("Tom 3") == "16 Floor Tom" && in ("Tom 4").isEmpty(),
           "toms smallest first");
    check (in ("Crash 1") == "16 Crash 3-zone" && in ("Crash 2") == "18 Crash" && in ("Aux 1") == "10 Splash", "cymbals");
    check (kit.getString ("insstCSCrash 1itp") == "Ride" && kit.getInt ("insstCSCrash 1artc") == 4
           && kit.getInts ("insstCSCrash 1azt") == std::vector<int> { 40, 42, 43, 41 }, "3-zone crash slot");
    check (kit.getString ("insstCSTom 1artn2") == "Rim Shot" && kit.getInt ("insstCSTom 1isld") == 1, "tom slot");
    check (in ("Kick Layer").isEmpty() && kit.getInt ("insstCSKick Layerisld") == 0 && kit.find ("CMEKick Layercv") == nullptr,
           "unused slot emptied");
    check (kit.getInt ("insstCSRide 2enb") == 0 && in ("Ride 2").isEmpty(), "disabled slot left alone");
    check (report.placed.size() == 9 && report.leftOut.isEmpty() && report.notes.isEmpty(), "report");

    check (kit.getInt ("CMEKicknm") == 1 && kit.find ("CMEKickmi1micv") == nullptr
           && kit.getString ("CMEKickmi0micn") == "Kick", "kick channel has the instrument's one mic");
    check (juce::exactlyEqual (kit.getFloat ("CMEKickmi0micp"), 0.5f) && juce::exactlyEqual (kit.getFloat ("CMEKickmi0micv"), 0.8f), "kick mic centred at the template level");
    check (kit.getInt ("CMEKickcmp0en") == 1, "kick channel keeps its compressor");
    check (juce::exactlyEqual (kit.getFloat ("CMETom 1cv"), 1.0f) && kit.getInt ("CMETom 1cmp0en") == 0 && kit.getInt ("CMETom 1eq0e5v1en") == 0,
           "new channel copied, EQ and compressor off");
    check (kit.find ("CMEKickscb") != nullptr && kit.find ("CMEKicksvr") != nullptr
           && kit.getString ("TPADKICKKICKfprn") == "R-KD-120", "buses and pads kept");
}

int buildKit (const juce::StringArray& args)
{
    try
    {
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::Array<juce::File> files;
        for (int i = 2; i < args.size(); ++i)
            files.add (cwd.getChildFile (args[i]));
        const auto report = MimicKit::buildFile (files, cwd.getChildFile (args[0]), cwd.getChildFile (args[1]));
        for (const auto& [slot, name] : report.placed)
            std::printf ("%-12s %s\n", slot.toRawUTF8(), name.toRawUTF8());
        for (const auto& name : report.leftOut)
            std::printf ("left out: %s\n", name.toRawUTF8());
        for (const auto& note : report.notes)
            std::printf ("note: %s\n", note.toRawUTF8());
        return 0;
    }
    catch (const mci::Error& e)
    {
        std::fprintf (stderr, "error: %s\n", e.what());
        return 1;
    }
}

int rewriteKit (const juce::String& in, const juce::String& out)
{
    try
    {
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        const auto result = mci::writeKitFile (mci::readSettings (cwd.getChildFile (in)), cwd.getChildFile (out));
        if (result.failed())
            throw mci::Error (result.getErrorMessage().toStdString());
        return 0;
    }
    catch (const mci::Error& e)
    {
        std::fprintf (stderr, "error: %s\n", e.what());
        return 1;
    }
}

int convert (const juce::StringArray& args)
{
    WavToMci::Options options;
    juce::Array<juce::File> inputs;
    juce::File outDir;
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a == "--out" && i + 1 < args.size())
            outDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
        else if (a == "--name" && i + 1 < args.size())
            options.name = args[++i];
        else if (a == "--no-choke")
            options.choke = false;
        else if (a == "--hat-as-ride")
            options.hatAsRide = true;
        else if (! a.startsWith ("--"))
            inputs.add (juce::File::getCurrentWorkingDirectory().getChildFile (a));
        else
        {
            std::fprintf (stderr, "unknown argument %s\n", a.toRawUTF8());
            return 2;
        }
    }
    if (inputs.isEmpty())
    {
        std::fprintf (stderr, "usage: --convert INPUT.zip|FOLDER [FOLDER...] [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]\n");
        return 2;
    }
    const auto report = WavToMci::run (inputs, outDir, options,
                                       [] (const juce::String& line) { std::printf ("%s\n", line.toRawUTF8()); std::fflush (stdout); },
                                       [] (double) { return true; });
    std::printf ("%s\n", report.toString().toRawUTF8());
    return report.foundHits && report.failed.isEmpty() && ! report.written.isEmpty() ? 0 : 1;
}

int rewrite (const juce::String& in, const juce::String& out)
{
    try
    {
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        const auto f = mci::readMci (cwd.getChildFile (in));
        const auto result = mci::writeMciFile (f, cwd.getChildFile (out));
        if (result.failed())
            throw mci::Error (result.getErrorMessage().toStdString());
        return 0;
    }
    catch (const mci::Error& e)
    {
        std::fprintf (stderr, "%s\n", e.what());
        return 1;
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args[0] == "--convert")
    {
        args.remove (0);
        return convert (args);
    }
    if (args[0] == "--rewrite" && args.size() == 3)
        return rewrite (args[1], args[2]);
    if (args[0] == "--rewrite-kit" && args.size() == 3)
        return rewriteKit (args[1], args[2]);
    if (args[0] == "--kit" && args.size() >= 4)
    {
        args.remove (0);
        return buildKit (args);
    }
    if (args.size() > 0 && args[0] != "--selftest")
    {
        std::fprintf (stderr, "usage: %s [--selftest | --convert INPUT [INPUT...] [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]"
                              " | --rewrite IN.mci OUT.mci"
                              " | --rewrite-kit IN.kit OUT.kit | --kit TEMPLATE.kit OUT.kit IN.mci...]\n", argv[0]);
        return 2;
    }

    try
    {
        checkCodec();
        checkBuilders();
        checkContainer();
        checkKits();
    }
    catch (const std::exception& e)
    {
        std::printf ("FAILED: %s\n", e.what());
        ++failures;
    }
    std::printf ("%s\n", failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
