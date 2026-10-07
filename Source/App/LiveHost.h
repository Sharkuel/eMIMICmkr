#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <bitset>

/** Runs the loaded plugin on the sound card, like a DAW track: MIDI from every input plays it,
    clicks in its own window are heard, and notes the plugin sends out (from its keyboard or kit
    view) are reported through onPluginNote, on the message thread. */
class LiveHost final : public juce::AudioIODeviceCallback,
                       public juce::MidiInputCallback,
                       private juce::Timer
{
public:
    LiveHost()
    {
        collector.reset (44100.0);   // until a device starts
        startTimerHz (30);
    }
    ~LiveHost() override { stopTimer(); }

    std::function<void (int note)> onPluginNote;

    /** While listening, each new hit the plugin plays with no MIDI input behind it (a click in its
        window) is captured and handed to onHeardHit on the message thread: mono, from the start
        of the hit, captureSeconds long, at rate. */
    std::function<void (const juce::AudioBuffer<float>& hit, double rate)> onHeardHit;
    static constexpr double captureSeconds = 0.3;
    void setListening (bool on)
    {
        capturePhase = armed;
        listening = on;
    }

    /** nullptr stops it. Once this returns, the audio thread no longer touches the old plugin. */
    void setProcessor (juce::AudioPluginInstance* p)
    {
        {
            const juce::ScopedLock sl (lock);
            processor = nullptr;
        }
        if (p == nullptr)
            return;
        if (device != nullptr)
            prepare (*p);
        const juce::ScopedLock sl (lock);
        processor = p;
    }

    juce::AudioPluginInstance* getProcessor() const { return processor; }

    /** Plays a message as if it came from a MIDI input. */
    void play (const juce::MidiMessage& m)
    {
        auto copy = m;
        copy.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        collector.addMessageToQueue (copy);
    }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        collector.addMessageToQueue (m);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const juce::ScopedLock sl (lock);
        device = d;
        collector.reset (d->getCurrentSampleRate());
        if (processor != nullptr)
            prepare (*processor);
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl (lock);
        device = nullptr;
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);

        midi.clear();
        collector.removeNextBlockOfMessages (midi, numSamples);

        const juce::ScopedTryLock sl (lock);
        if (! sl.isLocked() || processor == nullptr || numSamples > buffer.getNumSamples())
            return;

        std::bitset<128> sentIn;
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
                sentIn.set ((size_t) msg.getNoteNumber());
        }

        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
        block.clear();
        processor->processBlock (block, midi);

        // Notes the plugin plays from its own window come back out as MIDI.
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn() && ! sentIn.test ((size_t) msg.getNoteNumber()))
                pushNote (msg.getNoteNumber());
        }

        const int outs = processor->getMainBusNumOutputChannels();
        if (listening && outs > 0)
            listen (block, outs, numSamples, sentIn.any());
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr && outs > 0)
                juce::FloatVectorOperations::copy (outputs[c], block.getReadPointer (juce::jmin (c, outs - 1)),
                                                   numSamples);
    }

private:
    void prepare (juce::AudioPluginInstance& p)
    {
        const double rate = device->getCurrentSampleRate();
        const int size = device->getCurrentBufferSizeSamples();
        p.setNonRealtime (false);
        p.setRateAndBufferSizeDetails (rate, size);
        p.prepareToPlay (rate, size);
        buffer.setSize (juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), size);
        midi.ensureSize (4096);
        capture.setSize (1, (int) (captureSeconds * rate));
        captureRate = rate;
        capturePhase = armed;
    }

    /** Finds the start of a new hit by its jump over the last few milliseconds, so a click on the
        bell is caught even while the bow still rings. */
    void listen (const juce::AudioBuffer<float>& block, int outs, int numSamples, bool midiCameIn)
    {
        sinceMidiIn = midiCameIn ? 0 : sinceMidiIn + numSamples;
        auto* out = capture.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < outs; ++c)
                x += block.getSample (c, i);
            x /= (float) outs;
            recent[(size_t) (recentPos++ % recent.size())] = x;

            if (capturePhase == capturing)
            {
                out[captured++] = x;
                if (captured == capture.getNumSamples())
                {
                    // The ring that follows is not a new hit: measure the next ones against it.
                    history.fill (capture.getMagnitude (0, captured - chunk, chunk));
                    capturePhase = ready;
                }
                continue;
            }
            chunkPeak = juce::jmax (chunkPeak, std::abs (x));
            if (++chunkPos < chunk)
                continue;
            float before = 0.0f;
            for (auto p : history)
                before = juce::jmax (before, p);
            const bool onset = chunkPeak > 0.003f && chunkPeak > 4.0f * before
                               && sinceMidiIn > (int) (0.5 * captureRate);   // pads and keys are learned from MIDI
            for (size_t h = history.size() - 1; h > 0; --h)
                history[h] = history[h - 1];
            history[0] = chunkPeak;
            chunkPeak = 0.0f;
            chunkPos = 0;
            if (onset && capturePhase == armed && capture.getNumSamples() > chunk)
            {
                for (captured = 0; captured < chunk; ++captured)   // the chunk the hit started in
                    out[captured] = recent[(recentPos + recent.size() - (size_t) (chunk - captured)) % recent.size()];
                capturePhase = capturing;
            }
        }
    }

    void pushNote (int note)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0)
        {
            notes[(size_t) start1] = note;
            fifo.finishedWrite (1);
        }
    }

    void timerCallback() override { poll(); }

public:
    /** Hands what the audio thread caught to onPluginNote and onHeardHit; the timer calls it. */
    void poll()
    {
        if (capturePhase == ready)
        {
            if (onHeardHit)
                onHeardHit (capture, captureRate);
            capturePhase = armed;
        }

        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i)
            if (onPluginNote) onPluginNote (notes[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i)
            if (onPluginNote) onPluginNote (notes[(size_t) (start2 + i)]);
        fifo.finishedRead (size1 + size2);
    }

private:

    juce::CriticalSection lock;
    juce::AudioPluginInstance* processor = nullptr;
    juce::AudioIODevice* device = nullptr;
    juce::MidiMessageCollector collector;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    juce::AbstractFifo fifo { 64 };

    enum Phase { armed, capturing, ready };
    static constexpr int chunk = 256;
    std::atomic<bool> listening { false };
    std::atomic<int> capturePhase { armed };
    juce::AudioBuffer<float> capture;
    double captureRate = 44100.0;
    int captured = 0, chunkPos = 0, sinceMidiIn = 0;
    float chunkPeak = 0.0f;
    std::array<float, 6> history {};
    std::array<float, 1024> recent {};
    size_t recentPos = 0;
    std::array<int, 64> notes {};
};
