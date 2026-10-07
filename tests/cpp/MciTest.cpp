// Checks of the C++ .mci writer and WAV converter, and the converter's command line.
//
//   eMIMICmkr_mci_tests                      run the self-checks
//   eMIMICmkr_mci_tests --convert INPUT [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]
//                                            convert a folder or .zip of rendered WAVs
//   eMIMICmkr_mci_tests --rewrite IN OUT     read a .mci and write it back (reader/writer check)
//
// tests/test_cpp_converter.py compares --convert with tools/mci/wav2mci.py.
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
        hits.push_back ({ v, {} });
    const auto layers = WavToMci::splitLayers (hits);
    check (layers.size() == 20 && layers[0].size() == 5, "splitLayers");
    const auto vel = WavToMci::minVelocities (layers);
    check (vel[0] == 0 && vel[1] == 6 && vel[2] == 11, "minVelocities");

    check (WavToMci::pickOpenness ({ "tight", "closed2", "open1", "open2", "open3", "open4", "open5", "open6" })
               == juce::StringArray { "closed2", "open2", "open3", "open4", "open6" },
           "pickOpenness spreads closed to open");
    check (WavToMci::pickOpenness ({ "closed", "open1" }) == juce::StringArray { "closed", "open1", "open1", "open1", "open1" },
           "pickOpenness repeats the most open");
    check (WavToMci::classify ("RIDE1 BELL") == std::pair<juce::String, juce::String> ("cymbal", "bell"), "classify ride bell");
    check (WavToMci::classify ("HATS BOW OPEN3") == std::pair<juce::String, juce::String> ("hat", "bow:open3"), "classify hat");
    check (WavToMci::classify ("HATS FOOT SPLASH").second == "footsplash", "classify foot splash");
    check (WavToMci::classify ("TOM2 RIM").second == "rimshot", "classify tom rim");

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

int convert (const juce::StringArray& args)
{
    WavToMci::Options options;
    juce::File input, outDir;
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
        else if (input == juce::File() && ! a.startsWith ("--"))
            input = juce::File::getCurrentWorkingDirectory().getChildFile (a);
        else
        {
            std::fprintf (stderr, "unknown argument %s\n", a.toRawUTF8());
            return 2;
        }
    }
    if (input == juce::File())
    {
        std::fprintf (stderr, "usage: --convert INPUT.zip|FOLDER [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]\n");
        return 2;
    }
    const auto report = WavToMci::run (input, outDir, options,
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
    if (args.size() > 0 && args[0] != "--selftest")
    {
        std::fprintf (stderr, "usage: %s [--selftest | --convert INPUT [--out DIR] [--name NAME] [--no-choke] [--hat-as-ride]"
                              " | --rewrite IN.mci OUT.mci]\n", argv[0]);
        return 2;
    }

    try
    {
        checkCodec();
        checkBuilders();
        checkContainer();
    }
    catch (const std::exception& e)
    {
        std::printf ("FAILED: %s\n", e.what());
        ++failures;
    }
    std::printf ("%s\n", failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
