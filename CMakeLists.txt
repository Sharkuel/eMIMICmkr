cmake_minimum_required(VERSION 3.22)
project(eMIMICmkr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(eMIMICmkr
    PRODUCT_NAME "eMIMICmkr"
    COMPANY_NAME "eMIMICmkr")

target_sources(eMIMICmkr PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/PluginScanner.cpp
    Source/Core/KitMap.cpp
    Source/Core/HitRenderer.cpp
    Source/Core/BatchRecorder.cpp)

target_compile_definitions(eMIMICmkr PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:eMIMICmkr,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:eMIMICmkr,JUCE_VERSION>")

target_link_libraries(eMIMICmkr PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# A console check of the batch recorder against a stand-in plugin (tests/test_batch_recording.py runs it).
juce_add_console_app(eMIMICmkr_tests PRODUCT_NAME "eMIMICmkr_tests")
target_sources(eMIMICmkr_tests PRIVATE
    tests/cpp/BatchRecorderTest.cpp
    Source/Core/KitMap.cpp
    Source/Core/HitRenderer.cpp
    Source/Core/BatchRecorder.cpp)
target_compile_definitions(eMIMICmkr_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(eMIMICmkr_tests PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
