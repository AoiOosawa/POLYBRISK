cmake_minimum_required(VERSION 3.22)

# 対象環境 (SPEC §1 / docs/DECISIONS.md D-077)。project() より前に設定する。
# 開発用のビルドは arm64 だけ (速い)。配布用は scripts/package.sh が arm64 + x86_64 の Universal でビルドする。
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")

project(PolyBrisk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POLYBRISK_COPY_PLUGIN "Copy built AU/VST3 into ~/Library/Audio/Plug-Ins" ON)
option(POLYBRISK_BUILD_TESTS "Build the unit test runner" ON)

# ---------------------------------------------------------------------------
# JUCE 9 (コミットハッシュで固定。docs/DECISIONS.md D-001)
# ---------------------------------------------------------------------------
# include(FetchContent) より前に設定する (Debug / Release のビルドディレクトリで共有)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external/_deps" CACHE PATH "")
include(FetchContent)
set(POLYBRISK_JUCE_TAG "9.0.6")
set(POLYBRISK_JUCE_COMMIT "654bdbc7a9db0dae90849b44abe91f339e9f0768")
FetchContent_Declare(JUCE
    URL "https://github.com/juce-framework/JUCE/archive/${POLYBRISK_JUCE_COMMIT}.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# polybrisk_dsp: JUCE / UI / ファイルI/O / JSON に依存しない純粋な C++ DSP
# ---------------------------------------------------------------------------
add_library(polybrisk_dsp STATIC
    src/fx/FxChain.cpp
    src/rhythm/Arpeggiator.cpp
    src/synth/Envelopes.cpp
    src/synth/Filters.cpp
    src/synth/SynthEngine.cpp
    src/synth/Unison.cpp
    src/synth/Voice.cpp)
target_include_directories(polybrisk_dsp PUBLIC src)
target_compile_options(polybrisk_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
set_target_properties(polybrisk_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# プラグインとテストが共有する JUCE 依存ソース (state + processor)
# ---------------------------------------------------------------------------
set(POLYBRISK_SHARED_SOURCES
    src/state/MetaState.cpp
    src/state/Parameters.cpp
    src/state/PresetManager.cpp
    src/state/PresetSerializer.cpp
    src/state/StateSerializer.cpp
    src/ui/Controls.cpp
    src/ui/Theme.cpp
    src/ui/Views.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

# Factory プリセット (JSON) をバイナリに組み込む (SPEC §11.2: 読み取り専用の組込みリソース)
file(GLOB POLYBRISK_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/presets/factory/*.json")
juce_add_binary_data(PolyBriskPresets
    HEADER_NAME "PresetData.h"
    NAMESPACE PresetData
    SOURCES ${POLYBRISK_FACTORY_PRESETS})
set_target_properties(PolyBriskPresets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(POLYBRISK_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

# ---------------------------------------------------------------------------
# Plugin: AU / VST3 / Standalone
# 識別子は開発中の仮値 (docs/DECISIONS.md D-005)。配布前に必ず確定する。
# ---------------------------------------------------------------------------
juce_add_plugin(PolyBrisk
    PRODUCT_NAME "POLYBRISK"
    COMPANY_NAME "CHEEBOW"
    # 識別子は確定 (D-077)。配布後に変えると、利用者の DAW のプロジェクトからプラグインが見つからなくなる。
    # メーカーコード Chbw は、CHEEBOW のほかのプラグインと共通
    BUNDLE_ID "info.cheebow.polybrisk"
    PLUGIN_MANUFACTURER_CODE Chbw
    PLUGIN_CODE Pbrk
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/branding/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${POLYBRISK_COPY_PLUGIN})

target_sources(PolyBrisk PRIVATE ${POLYBRISK_SHARED_SOURCES})
target_include_directories(PolyBrisk PRIVATE src)
target_compile_definitions(PolyBrisk PUBLIC ${POLYBRISK_JUCE_DEFINITIONS})
target_link_libraries(PolyBrisk
    PRIVATE
        polybrisk_dsp
        PolyBriskPresets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: JUCE UnitTest ランナー (CTest に登録)
# ---------------------------------------------------------------------------
if(POLYBRISK_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(polybrisk_tests PRODUCT_NAME "polybrisk_tests")
    target_sources(polybrisk_tests PRIVATE
        ${POLYBRISK_SHARED_SOURCES}
        tests/TestMain.cpp
        tests/TestHelpers.cpp
        tests/dsp/EngineTests.cpp
        tests/dsp/EnvelopeTests.cpp
        tests/dsp/FilterTests.cpp
    tests/dsp/ModulationTests.cpp
        tests/dsp/HalfbandTests.cpp
        tests/dsp/OscillatorTests.cpp
        tests/dsp/TransientTests.cpp
        tests/dsp/UnisonTests.cpp
        tests/fx/FxTests.cpp
        tests/midi/PerformanceTests.cpp
        tests/rhythm/ArpeggiatorTests.cpp
        tests/rhythm/RhythmClockTests.cpp
        tests/rhythm/TranceGateTests.cpp
        tests/midi/ProcessorMidiTests.cpp
        tests/state/MacroTests.cpp
        tests/state/PresetTests.cpp
        tests/state/StateTests.cpp
        tests/state/ParameterRegistryTests.cpp
        tests/ui/KeyboardFocusTests.cpp)
    target_include_directories(polybrisk_tests PRIVATE src tests)
    target_compile_definitions(polybrisk_tests PRIVATE
        ${POLYBRISK_JUCE_DEFINITIONS}
        POLYBRISK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(polybrisk_tests
        PRIVATE
            polybrisk_dsp
            PolyBriskPresets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME polybrisk_tests COMMAND polybrisk_tests)

    # Factory プリセットを WAV に書き出す試聴ツール (音量の計測・UI のスクリーンショットも行う)。
    juce_add_console_app(polybrisk_render PRODUCT_NAME "polybrisk_render")
    target_sources(polybrisk_render PRIVATE
        ${POLYBRISK_SHARED_SOURCES}
        tests/TestHelpers.cpp
        tools/RenderPresets.cpp)
    target_include_directories(polybrisk_render PRIVATE src tests)
    target_compile_definitions(polybrisk_render PRIVATE ${POLYBRISK_JUCE_DEFINITIONS})
    target_link_libraries(polybrisk_render
        PRIVATE
            polybrisk_dsp
            PolyBriskPresets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # DSP ベンチマーク (JUCE 非依存)。scripts/bench.sh から Release で実行する。
    add_executable(polybrisk_bench tests/bench/Benchmark.cpp)
    target_link_libraries(polybrisk_bench PRIVATE polybrisk_dsp)
endif()
