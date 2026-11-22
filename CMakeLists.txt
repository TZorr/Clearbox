# ==============================================================================
#
#  Clearbox
#
#  A target-curve dynamic EQ. It measures the spectrum over about two seconds
#  and pulls it towards a rounded 4.5 dB/oct curve - Lift raises what is below,
#  Trim takes off what is above - while the loudness stays where it was. Three
#  soft bells after it, their Q following their gain.
#
#  Three build products: AU (aufx/Clbx/Tzor, the deliverable for Logic), VST3
#  and Standalone (the development build). No AUv3, as in the rest of the family.
#
#  Two console targets do the verifying. ClearboxCheck measures the engine
#  against signals whose answer is known; EditorShot goes through the whole
#  processor (state, presets, bypass, latency) and renders the panel to a PNG.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(Clearbox VERSION 0.9.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon only, matching the other plugin bundles on this machine.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.5" CACHE STRING "" FORCE)

set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE framework")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Pass -DJUCE_PATH=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------

juce_add_plugin(Clearbox
    # The identity is the pair of four-character codes: aufx/Clbx/Tzor. Changing
    # either after a session has been saved makes a different plugin.
    COMPANY_NAME                "T'Zorr"
    BUNDLE_ID                   TZorr.Clearbox
    PLUGIN_MANUFACTURER_CODE    Tzor
    PLUGIN_CODE                 Clbx
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "Clearbox"
    DESCRIPTION                 "Target-curve dynamic EQ"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE    # Scripts/install.sh does this, cache flush and all
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    HARDENED_RUNTIME_ENABLED    TRUE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# The engine is plain C++ and Accelerate; the check links exactly these objects.
set(CLEARBOX_CORE_SOURCES
    Source/Engine/ClearEngine.cpp
    Source/Engine/ResonanceStage.cpp
    Source/Engine/SoftEq.cpp)

set(CLEARBOX_UI_SOURCES
    Source/UI/Knob.cpp
    Source/UI/Controls.cpp
    Source/UI/CurveDisplay.cpp
    Source/UI/PresetButton.cpp)

target_sources(Clearbox PRIVATE
    Source/Engine/PresetFile.cpp
    ${CLEARBOX_CORE_SOURCES}
    ${CLEARBOX_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Clearbox PRIVATE Source)

target_compile_definitions(Clearbox PUBLIC
    TZORR_PRESET_EXTENSION=\".clearbox\"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Clearbox PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    "-framework Accelerate"
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# ClearboxCheck - the engine harness
# ------------------------------------------------------------------------------

juce_add_console_app(ClearboxCheck PRODUCT_NAME "ClearboxCheck")

target_sources(ClearboxCheck PRIVATE
    Verification/ClearboxCheck.cpp
    ${CLEARBOX_CORE_SOURCES})

target_include_directories(ClearboxCheck PRIVATE Source)

target_compile_definitions(ClearboxCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(ClearboxCheck PRIVATE
    juce::juce_core
    "-framework Accelerate"
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# EditorShot - the whole processor, and a picture of the panel
# ------------------------------------------------------------------------------

juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")

target_sources(EditorShot PRIVATE Verification/EditorShot.cpp)

target_compile_definitions(EditorShot PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(EditorShot PRIVATE
    Clearbox
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_include_directories(EditorShot PRIVATE Source)

target_compile_options(EditorShot PRIVATE -Wno-macro-redefined)
