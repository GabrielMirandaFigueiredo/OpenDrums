include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# AU only exists on macOS
if(APPLE)
    set(OPENDRUMS_FORMATS VST3 AU Standalone)
else()
    set(OPENDRUMS_FORMATS VST3 Standalone)
endif()

juce_add_plugin(OpenDrums
    COMPANY_NAME              "OpenDrums"
    BUNDLE_ID                 "org.opendrums.OpenDrums"
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice
    PRODUCT_NAME              "OpenDrums"
    PLUGIN_MANUFACTURER_CODE  Odrm
    PLUGIN_CODE               Odr1
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS                   ${OPENDRUMS_FORMATS}
    VST3_CATEGORIES           Instrument Drum
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(OpenDrums PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    KitView.cpp
    Look.cpp
    MixView.cpp
    GroovesView.cpp
    MatrixView.cpp)

target_compile_definitions(OpenDrums PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(OpenDrums PRIVATE /utf-8) # sources contain UTF-8 (pt-BR UI text)
endif()

target_link_libraries(OpenDrums
    PRIVATE
        opendrums_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless test of the processor (audio path, routing, groove player, state). Not shipped.
if(OPENDRUMS_BUILD_TESTS)
    juce_add_console_app(OpenDrumsProcessorTest PRODUCT_NAME "OpenDrumsProcessorTest")
    target_sources(OpenDrumsProcessorTest PRIVATE
        ../tests/processor_test.cpp PluginProcessor.cpp PluginEditor.cpp KitView.cpp Look.cpp MixView.cpp GroovesView.cpp MatrixView.cpp)
    target_include_directories(OpenDrumsProcessorTest PRIVATE .)
    target_compile_definitions(OpenDrumsProcessorTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(OpenDrumsProcessorTest PRIVATE opendrums_core juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
