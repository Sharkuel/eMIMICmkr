# Makes JUCE's VST3 host hold the plugin's callback lock while it restarts a plugin that asked for
# it (new outputs, reload). JUCE does that restart on the message thread while the sound card
# thread may be inside processBlock; with yabridge, which remaps its shared audio buffers on a
# restart, that crashes (MODO DRUM, whose window asks for one when it resizes). The live host takes
# the same lock around processBlock, so the two never overlap.
#
# Run with -DJUCE_DIR=<JUCE source> (cmake -P), or include()d with JUCE_DIR set. Patches once.
set(file "${JUCE_DIR}/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp")
file(READ "${file}" text)
set(marker "eMIMICmkr: callback lock")
if(NOT text MATCHES "${marker}")
    set(before "${text}")
    string(REPLACE
        "        plugin->reset();"
        "        { const ScopedLock callbackLock (plugin->getCallbackLock()); plugin->reset(); } // ${marker}"
        text "${text}")
    string(REPLACE
        "        plugin->releaseResources();"
        "        const ScopedLock callbackLock (plugin->getCallbackLock()); // ${marker}\n        plugin->releaseResources();"
        text "${text}")
    if(text STREQUAL before)
        message(FATAL_ERROR "PatchJuce.cmake: this JUCE version's VST3 host doesn't look as expected; update the patch.")
    endif()
    file(WRITE "${file}" "${text}")
endif()
