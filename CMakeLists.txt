cmake_minimum_required(VERSION 3.22)

# HPDG 808: the HPDG bass engine as its own instrument (VST3 + Standalone, Windows x64).
# The version is written only here; Installer/Build-HPDG808.ps1 reads it.
set(HPDG808_VERSION 1.0.2)
project(HPDG808 VERSION ${HPDG808_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_TARGET_ARCHITECTURE "x86_64")

# JUCE 8.0.12, vendored in JUCE/
add_subdirectory(JUCE)

juce_add_plugin(HPDG808
    VERSION ${HPDG808_VERSION}
    COMPANY_NAME "HamloProd"
    BUNDLE_ID com.boombaplabs.hpdg808
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE BBLB
    PLUGIN_CODE H808
    FORMATS Standalone VST3
    VST3_CATEGORIES Instrument Synth
    PRODUCT_NAME "HPDG 808"
)

if (TARGET HPDG808_VST3)
    target_compile_definitions(HPDG808_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
endif()

target_sources(HPDG808
    PRIVATE
        Source/Bass808/Bass808Processor.cpp
        Source/Bass808/Bass808Editor.cpp
        Source/Bass808/Bass808Screen.cpp
        Source/Bass808/BassEngine.cpp
        Source/Bass808/BassSample.cpp
        Source/Bass808/BassSettingsIO.cpp
        Source/Bass808/BassSaturator.cpp
        Source/Bass808/BassMaster.cpp
        Source/Bass808/BassExport.cpp
        Source/Audio/Synth808Voice.cpp
        Source/UI/SketchDrawing.cpp
)

target_include_directories(HPDG808 PRIVATE Source)

target_compile_definitions(HPDG808
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(HPDG808
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

