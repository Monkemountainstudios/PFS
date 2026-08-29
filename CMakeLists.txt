cmake_minimum_required(VERSION 3.22)

project(PFS VERSION 0.6.11 LANGUAGES C CXX)

set(JUCE_PATH "F:/JUCE" CACHE PATH "Path to a JUCE source checkout")
if (EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory ("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(PFSAssets SOURCES
    audio/sound1.ogg
    audio/sound2.ogg
    audio/sound3.ogg
    audio/sound4.ogg
    audio/sound5.ogg
    audio/fuap.ogg
)

set(PFS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PFS_FORMATS AU)
endif()

juce_add_plugin(PFS
    COMPANY_NAME "Monke Mountain Studios"
    BUNDLE_ID com.monkemountainstudios.pfs
    PLUGIN_MANUFACTURER_CODE MkMt
    PLUGIN_CODE PfSq
    FORMATS ${PFS_FORMATS}
    PRODUCT_NAME "PFS"
    DESCRIPTION "Probabilistic Fractal Sequencer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(PFS)

target_sources(PFS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SequencerEngine.h
)

target_sources(PFS_Standalone PRIVATE Source/StandaloneApp.cpp)
target_compile_definitions(PFS_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
if(MSVC)
    target_link_options(PFS_Standalone PRIVATE "/MAP:${CMAKE_BINARY_DIR}/PFS_Standalone.map")
endif()

target_compile_features(PFS PUBLIC cxx_std_20)
target_compile_definitions(PFS PUBLIC
    JUCE_ASIO=1
    JUCE_REPORT_APP_USAGE=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(PFS PRIVATE
    PFSAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_warning_flags
)

# CMake's normal Release/Debug optimisation flags are sufficient here. JUCE's
# recommended config target also enables MSVC /MP, which can launch many compiler
# processes and exhaust memory on a full plug-in build.

# Keep MSVC whole-program optimisation disabled until the earlier release-only
# editor crash has been rechecked against JUCE 9.

option(PFS_BUILD_TESTS "Build the sequencer unit tests" ON)
if(PFS_BUILD_TESTS)
    enable_testing()
    add_executable(PFSSequencerTests Tests/SequencerEngineTests.cpp)
    target_compile_features(PFSSequencerTests PRIVATE cxx_std_20)
    target_include_directories(PFSSequencerTests PRIVATE Source)
    add_test(NAME PFSSequencerTests COMMAND PFSSequencerTests)

    add_executable(PFSProcessorTimingTests Tests/ProcessorTimingTests.cpp)
    target_compile_features(PFSProcessorTimingTests PRIVATE cxx_std_20)
    target_include_directories(PFSProcessorTimingTests PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/PFS_artefacts/JuceLibraryCode"
    )
    target_link_libraries(PFSProcessorTimingTests PRIVATE
        PFS
        PFSAssets
        juce::juce_audio_utils
        juce::juce_dsp
    )
    add_test(NAME PFSProcessorTimingTests COMMAND PFSProcessorTimingTests)

    if(WIN32)
        juce_add_console_app(PFSDeviceProbe PRODUCT_NAME "PFS Device Probe")
        target_sources(PFSDeviceProbe PRIVATE Tests/AudioDeviceProbe.cpp)
        target_link_libraries(PFSDeviceProbe PRIVATE juce::juce_audio_utils)
        target_compile_definitions(PFSDeviceProbe PRIVATE JUCE_ASIO=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    endif()
endif()
