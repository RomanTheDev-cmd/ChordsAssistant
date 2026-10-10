cmake_minimum_required(VERSION 3.22)
project(HarmonicShadow VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(HarmonicShadowEngine STATIC
    Source/HarmonicEngine.cpp
    Source/CaptureCoordinator.cpp
)
target_include_directories(HarmonicShadowEngine PUBLIC Source)
target_compile_options(HarmonicShadowEngine PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

option(HARMONIC_SHADOW_BUILD_PLUGIN "Build the Logic Pro AU MIDI FX plugin" OFF)
option(HARMONIC_SHADOW_BUILD_TESTS "Build deterministic harmony engine tests" ON)

if (HARMONIC_SHADOW_BUILD_TESTS)
    enable_testing()
    add_executable(HarmonicEngineTests Tests/HarmonicEngineTests.cpp)
    target_link_libraries(HarmonicEngineTests PRIVATE HarmonicShadowEngine)
    add_test(NAME HarmonicEngineExamples COMMAND HarmonicEngineTests)
endif()

if (HARMONIC_SHADOW_BUILD_PLUGIN)
    if (APPLE)
        set(HARMONIC_SHADOW_PLUGIN_FORMATS AU)
    else()
        # VST3 here is only for cross-platform compile/host checks; Logic shipping remains AU.
        set(HARMONIC_SHADOW_PLUGIN_FORMATS VST3)
        message(STATUS "Non-macOS plugin build is VST3 validation only; Logic Pro requires the AU target built on macOS.")
    endif()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(HarmonicShadow
        COMPANY_NAME "RomanTheDev"
        BUNDLE_ID "com.romanthedev.harmonicshadow"
        PLUGIN_MANUFACTURER_CODE RoTh
        PLUGIN_CODE HmSh
        PRODUCT_NAME "Harmonic Shadow"
        VERSION "0.2.0"
        FORMATS ${HARMONIC_SHADOW_PLUGIN_FORMATS}
        IS_MIDI_EFFECT TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_SYNTH FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    juce_generate_juce_header(HarmonicShadow)
    target_sources(HarmonicShadow PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.h
    )
    target_link_libraries(HarmonicShadow PRIVATE
        HarmonicShadowEngine
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    )
    target_compile_definitions(HarmonicShadow PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
endif()
