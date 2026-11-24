#include "HitRenderer.h"

namespace emk
{
namespace
{
float peakOf (const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = juce::jmax (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
    return peak;
}
} // namespace

HitRenderer::HitRenderer (juce::AudioPluginInstance& p, Settings s) : plugin (p), settings (s)
{
    // Multi-out drum plugins (SD3, BFD3...) leave their extra outputs off until a host asks.
    if (settings.mics.enabled())
    {
        plugin.releaseResources();
        plugin.enableAllBuses();
    }
    plugin.setNonRealtime (true);
    plugin.setRateAndBufferSizeDetails (settings.sampleRate, settings.blockSize);
    plugin.prepareToPlay (settings.sampleRate, settings.blockSize);
}

HitRenderer::~HitRenderer()
{
    plugin.releaseResources();
    plugin.setNonRealtime (false);
}

void HitRenderer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const juce::ScopedLock lock (plugin.getCallbackLock());
    plugin.processBlock (buffer, midi);
    midi.clear();
}

void HitRenderer::waitForSilence()
{
    // Let the previous hit ring out so it never bleeds into the next recording.
    const int channels = busChannels();
    juce::AudioBuffer<float> buffer (channels, settings.blockSize);
    juce::MidiBuffer midi;
    const float threshold = juce::Decibels::decibelsToGain ((float) settings.floorDb);
    const int maxBlocks = (int) (settings.maxSeconds * settings.sampleRate / settings.blockSize);
    for (int i = 0; i < maxBlocks; ++i)
    {
        processBlock (buffer, midi);
        if (peakOf (buffer) < threshold)
            return;
    }
}

void HitRenderer::addMainOutput (juce::AudioBuffer<float>& dest, int destStart,
                                 const juce::AudioBuffer<float>& src, int n)
{
    // Average the main stereo (or mono) output: a centred mono mix, whatever the plugin's panning.
    // Extra outputs (SD3's multi-out busses) are left out so they can't double or thin the sound.
    auto* bus = plugin.getBus (false, 0);
    const int outs = juce::jlimit (1, src.getNumChannels(), bus != nullptr ? bus->getNumberOfChannels() : 2);
    for (int ch = 0; ch < outs; ++ch)
        dest.addFrom (0, destStart, src, ch, 0, n, 1.0f / (float) outs);
}

juce::StringArray HitRenderer::outputBusNames (juce::AudioPluginInstance& plugin)
{
    juce::StringArray names;
    for (int b = 0; b < plugin.getBusCount (false); ++b)
    {
        const auto* bus = plugin.getBus (false, b);
        auto name = bus != nullptr ? bus->getName().trim() : juce::String();
        names.add (name.isNotEmpty() ? name : (b == 0 ? juce::String ("Main") : "Output " + juce::String (b + 1)));
    }
    return names;
}

int HitRenderer::busChannels() const
{
    // Every bus, enabled or not, so the plugin can write wherever its layout says.
    int n = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    for (int b = 0; b < plugin.getBusCount (false); ++b)
        if (const auto* bus = plugin.getBus (false, b); bus != nullptr && bus->isEnabled())
            n = juce::jmax (n, bus->getChannelIndexInProcessBlockBuffer (0) + bus->getNumberOfChannels());
    return n;
}

void HitRenderer::addMics (juce::AudioBuffer<float>& dest, int destStart, const juce::AudioBuffer<float>& src, int n)
{
    // Channels: 0 close (mono), 1-2 overheads, 3-4 room.
    auto busRange = [&] (int index, int& first, int& count)
    {
        first = count = 0;
        const auto* bus = index >= 0 ? plugin.getBus (false, index) : nullptr;
        if (bus == nullptr || ! bus->isEnabled())
            return false;
        first = bus->getChannelIndexInProcessBlockBuffer (0);
        count = juce::jmin (bus->getNumberOfChannels(), src.getNumChannels() - first);
        return count > 0;
    };
    int first = 0, count = 0;
    if (busRange (settings.mics.close, first, count))
        for (int c = 0; c < count; ++c)
            dest.addFrom (0, destStart, src, first + c, 0, n, 1.0f / (float) count);
    const std::pair<int, int> pairs[] = { { settings.mics.overheads, 1 }, { settings.mics.room, 3 } };
    for (const auto& [bus, to] : pairs)
        if (busRange (bus, first, count))
        {
            dest.addFrom (to, destStart, src, first, 0, n);
            dest.addFrom (to + 1, destStart, src, first + (count > 1 ? 1 : 0), 0, n);
        }
}

juce::AudioBuffer<float> HitRenderer::mixDown (const juce::AudioBuffer<float>& hit)
{
    if (hit.getNumChannels() != 5)
        return hit;
    juce::AudioBuffer<float> mono (1, hit.getNumSamples());
    mono.copyFrom (0, 0, hit, 0, 0, hit.getNumSamples());
    for (int c = 1; c < 5; ++c)
        mono.addFrom (0, 0, hit, c, 0, hit.getNumSamples(), 0.5f);
    return mono;
}

juce::AudioBuffer<float> HitRenderer::renderHit (int note, int velocity, int cc4)
{
    if (cc4 >= 0 && cc4 != lastCc4)
    {
        lastCc4 = cc4;
        // Move the pedal first; any sound that makes (a pedal chick) rings out before the hit.
        const int channels = busChannels();
        juce::AudioBuffer<float> buffer (channels, settings.blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, juce::jlimit (0, 127, cc4)), 0);
        processBlock (buffer, midi);
    }
    waitForSilence();

    const int channels = busChannels();
    const int block = settings.blockSize;
    const int maxSamples = (int) (settings.maxSeconds * settings.sampleRate);
    const int noteOffAt = (int) (settings.noteLengthSeconds * settings.sampleRate);
    const int silentNeeded = (int) (settings.silenceSeconds * settings.sampleRate);
    const float threshold = juce::Decibels::decibelsToGain ((float) settings.silenceDb);
    // The tail is kept down to tailDb below the hit's peak, like SDSE's hits, so a soft hit's
    // ring isn't cut off where a loud one's would still be ringing (Alexandre 2026-10-14);
    // never above the start threshold, never below floorDb.
    const float tailGain = juce::Decibels::decibelsToGain ((float) settings.tailDb);
    const float floor = juce::Decibels::decibelsToGain ((float) settings.floorDb);
    float peak = 0.0f;

    const bool mics = settings.mics.enabled();
    juce::AudioBuffer<float> recorded (mics ? 5 : 1, maxSamples);
    recorded.clear();
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) velocity), 0);

    int written = 0, silentRun = 0, firstSound = -1, lastSound = 0;
    while (written < maxSamples)
    {
        if (written <= noteOffAt && noteOffAt < written + block)
            midi.addEvent (juce::MidiMessage::noteOff (10, note), noteOffAt - written);

        processBlock (buffer, midi);
        const int n = juce::jmin (block, maxSamples - written);
        if (mics)
            addMics (recorded, written, buffer, n);
        else
            addMainOutput (recorded, written, buffer, n);

        for (int i = 0; i < n; ++i)
        {
            // With separate mics, the start and the end of the hit are found on their mix.
            const int at = written + i;
            const float level = std::abs (mics ? recorded.getSample (0, at)
                                                     + 0.5f * (recorded.getSample (1, at) + recorded.getSample (2, at)
                                                               + recorded.getSample (3, at) + recorded.getSample (4, at))
                                               : recorded.getSample (0, at));
            peak = juce::jmax (peak, level);
            const float end = juce::jlimit (floor, threshold, peak * tailGain);
            if (level >= (firstSound < 0 ? threshold : end))
            {
                if (firstSound < 0)
                    firstSound = written + i;
                lastSound = written + i;
                silentRun = 0;
            }
            else if (firstSound >= 0)
            {
                ++silentRun;
            }
        }
        written += n;
        if (firstSound >= 0 && silentRun >= silentNeeded)
            break;
        if (firstSound < 0 && written >= settings.giveUpSeconds * settings.sampleRate)
            break;
    }

    if (firstSound < 0)
        return {};

    const int length = lastSound - firstSound + 1;
    juce::AudioBuffer<float> hit (recorded.getNumChannels(), length);
    for (int c = 0; c < recorded.getNumChannels(); ++c)
        hit.copyFrom (c, 0, recorded, c, firstSound, length);
    return hit;
}
} // namespace emk
