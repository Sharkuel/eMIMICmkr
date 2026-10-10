# Two small fixes to JUCE's VST3 host, both for plugins run through yabridge:
#
# - It gives a plugin's unused buses (inactive outputs) null channel pointers. yabridge copies the
#   plugin's output into every channel of every bus, so a plugin with spare outputs (MODO DRUM has
#   16 stereo ones) crashed the audio thread. Unused channels now point at a scratch buffer.
# - It restarts a plugin that asks for it (new outputs, reload) on the message thread while the
#   sound card thread may be inside processBlock. It now holds the plugin's callback lock while it
#   does, and the live host takes the same lock around processBlock.
#
# Run with -DJUCE_DIR=<JUCE source> (cmake -P), or include()d with JUCE_DIR set. Patches once.
# The patterns leave out line ends, as JUCE's sources use CRLF.
set(marker "eMIMICmkr:")

# replace(<text variable> <from> <to>): fails the configure if <from> isn't there.
macro(replace_once var from to)
    string(FIND "${${var}}" "${from}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "PatchJuce.cmake: JUCE doesn't look as expected for this version; update the patch.")
    endif()
    string(REPLACE "${from}" "${to}" ${var} "${${var}}")
endmacro()

set(dir "${JUCE_DIR}/modules/juce_audio_processors/format_types")

file(READ "${dir}/juce_VST3Common.h" text)
if(NOT text MATCHES "${marker}")
    replace_once(text
        "                                             : nullptr);"
        "                                             : spareChannel<FloatType> (buffer.getNumSamples())); // ${marker} unused buses get a scratch buffer")
    replace_once(text
        "    auto& get (detail::Tag<float>)    { return floatBusMap; }"
        "    // ${marker} somewhere for unused channels to point, so a plugin (or yabridge) writing to them is harmless.
    template <typename FloatType>
    FloatType* spareChannel (int numSamples) const
    {
        auto& spare = std::get<std::vector<FloatType>> (spares);
        if (spare.size() < (size_t) numSamples)
            spare.resize ((size_t) numSamples);
        std::fill (spare.begin(), spare.begin() + numSamples, FloatType{});
        return spare.data();
    }

    mutable std::tuple<std::vector<float>, std::vector<double>> spares;

    auto& get (detail::Tag<float>)    { return floatBusMap; }")
    file(WRITE "${dir}/juce_VST3Common.h" "${text}")
endif()

file(READ "${dir}/juce_VST3PluginFormat.cpp" text)
if(NOT text MATCHES "${marker}")
    replace_once(text
        "        plugin->reset();"
        "        { const ScopedLock callbackLock (plugin->getCallbackLock()); plugin->reset(); } // ${marker} callback lock")
    replace_once(text
        "        plugin->releaseResources();"
        "        const ScopedLock callbackLock (plugin->getCallbackLock()); // ${marker} callback lock\n        plugin->releaseResources();")
    file(WRITE "${dir}/juce_VST3PluginFormat.cpp" "${text}")
endif()
