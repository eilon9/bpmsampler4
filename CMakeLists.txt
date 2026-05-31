cmake_minimum_required(VERSION 3.22)
project(BPMSampler4 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# JUCE must be cloned into ./JUCE or installed system-wide
# To add as submodule: git submodule add https://github.com/juce-framework/JUCE.git JUCE
add_subdirectory(JUCE)

juce_add_plugin(BPMSampler4
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE Bcmp
    PLUGIN_CODE Bsm4
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "BPMSampler4"
    VST3_CATEGORIES "Instrument" "Sampler"
)

juce_generate_juce_header(BPMSampler4)

target_sources(BPMSampler4 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WaveformDisplay.cpp
    Source/CurveEditorComponent.cpp
    Source/CurveKnob.cpp
)

target_compile_definitions(BPMSampler4
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(BPMSampler4
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
