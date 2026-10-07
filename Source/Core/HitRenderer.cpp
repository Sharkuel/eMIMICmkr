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
    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, settings.blockSize);
    juce::MidiBuffer midi;
    const float threshold = juce::Decibels::decibelsToGain ((float) settings.silenceDb);
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

juce::AudioBuffer<float> HitRenderer::renderHit (int note, int velocity, int cc4)
{
    if (cc4 >= 0)
    {
        // Move the pedal first; any sound that makes (a pedal chick) rings out before the hit.
        const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, settings.blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, juce::jlimit (0, 127, cc4)), 0);
        processBlock (buffer, midi);
    }
    waitForSilence();

    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    const int block = settings.blockSize;
    const int maxSamples = (int) (settings.maxSeconds * settings.sampleRate);
    const int noteOffAt = (int) (settings.noteLengthSeconds * settings.sampleRate);
    const int silentNeeded = (int) (settings.silenceSeconds * settings.sampleRate);
    const float threshold = juce::Decibels::decibelsToGain ((float) settings.silenceDb);

    juce::AudioBuffer<float> recorded (1, maxSamples);
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
        addMainOutput (recorded, written, buffer, n);

        for (int i = 0; i < n; ++i)
        {
            if (std::abs (recorded.getSample (0, written + i)) >= threshold)
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
    }

    if (firstSound < 0)
        return {};

    const int length = lastSound - firstSound + 1;
    juce::AudioBuffer<float> hit (1, length);
    hit.copyFrom (0, 0, recorded, 0, firstSound, length);
    return hit;
}
} // namespace emk
