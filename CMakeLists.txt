cmake_minimum_required(VERSION 3.22)
project(eMIMICmkr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The .mci writer must round floats exactly as tools/mci/mci.py does (loudness envelopes,
# choke samples), so a compiler may not fuse a * b + c into one multiply-add there.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(Source/Core/Mci.cpp Source/Core/WavToMci.cpp
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(eMIMICmkr
    PRODUCT_NAME "eMIMICmkr"
    COMPANY_NAME "eMIMICmkr"
    ICON_BIG assets/logo.png)

# The logo for the window icon (macOS and Windows also take it from ICON_BIG), and the start-up banner.
juce_add_binary_data(eMIMICmkr_assets SOURCES assets/logo.png assets/banner.png)

target_sources(eMIMICmkr PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/PluginScanner.cpp
    Source/App/ScreenTools.cpp
    Source/Core/KitMap.cpp
    Source/Core/HitRenderer.cpp
    Source/Core/BatchRecorder.cpp
    Source/Core/Mci.cpp
    Source/Core/WavToMci.cpp
    Source/Core/SoundPrint.cpp
    Source/Core/MimicImage.cpp)

target_compile_definitions(eMIMICmkr PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:eMIMICmkr,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:eMIMICmkr,JUCE_VERSION>")

target_link_libraries(eMIMICmkr PRIVATE
    eMIMICmkr_assets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_audio_processors
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# A console check of the batch recorder against a stand-in plugin (tests/test_batch_recording.py runs it).
juce_add_console_app(eMIMICmkr_tests PRODUCT_NAME "eMIMICmkr_tests")
target_sources(eMIMICmkr_tests PRIVATE
    tests/cpp/BatchRecorderTest.cpp
    Source/Core/KitMap.cpp
    Source/Core/HitRenderer.cpp
    Source/Core/BatchRecorder.cpp
    Source/Core/Mci.cpp
    Source/Core/WavToMci.cpp
    Source/Core/SoundPrint.cpp
    Source/Core/MimicImage.cpp
    Source/App/ScreenTools.cpp)
target_compile_definitions(eMIMICmkr_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(eMIMICmkr_tests PRIVATE
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Checks of the .mci writer and WAV converter, and the converter's command line:
#   eMIMICmkr_mci_tests --convert INPUT [--out DIR] [--no-choke] [--hat-as-ride]
# (tests/test_cpp_converter.py compares it with tools/mci/wav2mci.py).
juce_add_console_app(eMIMICmkr_mci_tests PRODUCT_NAME "eMIMICmkr_mci_tests")
target_sources(eMIMICmkr_mci_tests PRIVATE
    tests/cpp/MciTest.cpp
    Source/Core/Mci.cpp
    Source/Core/WavToMci.cpp)
target_compile_definitions(eMIMICmkr_mci_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(eMIMICmkr_mci_tests PRIVATE
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
