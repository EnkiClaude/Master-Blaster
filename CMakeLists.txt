cmake_minimum_required(VERSION 3.22)
project(MasterBlaster VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

option(MB_BUILD_TESTS "Build the DSP test suite" ON)

# JUCE: pass -DJUCE_DIR=/path/to/JUCE to use a local checkout; otherwise it is fetched.
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MB_FORMATS AU)
endif()

juce_add_plugin(MasterBlaster
    COMPANY_NAME "Master Blaster"
    BUNDLE_ID "com.masterblaster.masterblaster"
    PLUGIN_MANUFACTURER_CODE Mbls
    PLUGIN_CODE Mbm1
    FORMATS ${MB_FORMATS}
    PRODUCT_NAME "Master Blaster"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Mastering Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MasterBlaster PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(MasterBlaster PRIVATE Source)

target_compile_definitions(MasterBlaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MasterBlaster
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MB_BUILD_TESTS)
    add_executable(mb_dsp_tests tests/dsp_tests.cpp)
    target_compile_options(mb_dsp_tests PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)
    enable_testing()
    add_test(NAME dsp COMMAND mb_dsp_tests)
endif()

# Optional: a minimal plugin host that loads the built VST3 and checks it.
option(MB_BUILD_HOST_CHECK "Build the VST3 host check tool" OFF)
if(MB_BUILD_HOST_CHECK)
    juce_add_console_app(mb_host_check PRODUCT_NAME "mb_host_check")
    target_sources(mb_host_check PRIVATE tools/host_check/HostCheck.cpp)
    target_compile_definitions(mb_host_check PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(mb_host_check PRIVATE
        juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags)

    juce_add_console_app(mb_ui_snapshot PRODUCT_NAME "mb_ui_snapshot")
    target_sources(mb_ui_snapshot PRIVATE tools/host_check/UiSnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(mb_ui_snapshot PRIVATE Source)
    target_compile_definitions(mb_ui_snapshot PRIVATE
        JucePlugin_Name="Master Blaster" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(mb_ui_snapshot PRIVATE
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
