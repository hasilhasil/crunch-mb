cmake_minimum_required(VERSION 3.24)
project(CrunchMB VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE 8.0.4 内嵌源码树（兄弟项目共享；可 -DJUCE_DIR= 覆盖）
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/JUCE-8.0.4")
endif()

# 字体共享自兄弟项目（含空格路径，已验证可用）
set(FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/src/ui/Fonts")

add_subdirectory("${JUCE_DIR}" JUCE)

# ---------- 字体二进制数据 ----------
set(FONT_SOURCES
    "${FONT_DIR}/Poppins-Regular.ttf"
    "${FONT_DIR}/Poppins-Medium.ttf"
    "${FONT_DIR}/Poppins-SemiBold.ttf"
    "${FONT_DIR}/Poppins-Bold.ttf")
juce_add_binary_data(CrunchMBData SOURCES ${FONT_SOURCES})

# ---------- 插件目标 ----------
juce_add_plugin(CrunchMB
    PRODUCT_NAME              "Crunch MB"
    COMPANY_NAME              "Kilo"
    BUNDLE_ID                 com.kilo.crunchmb
    PLUGIN_MANUFACTURER_CODE  Kilo
    PLUGIN_CODE               CRMB
    FORMATS                   VST3 Standalone
    VERSION                   1.0.0
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Dynamics)

target_sources(CrunchMB PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/dsp/BandCompressor.cpp
    src/dsp/CrossoverBank.cpp
    src/dsp/LinearPhaseCrossover.cpp
    src/dsp/MultibandDSP.cpp
    src/dsp/SideChainFilter.h
    src/dsp/SpectrumAnalyzer.cpp
    src/dsp/SpectrumEngine.cpp
    src/ui/Anim.h
    src/ui/DisplayView.cpp
    src/ui/Knob.cpp)

target_compile_definitions(CrunchMB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

juce_generate_juce_header(CrunchMB)

target_link_libraries(CrunchMB
    PRIVATE
        CrunchMBData
        juce::juce_dsp
    PUBLIC
        juce::juce_audio_utils)

# ---------- 编译选项（TECH_SPEC §1） ----------
if(MSVC)
    target_compile_options(CrunchMB PRIVATE /utf-8 /permissive-)
else()
    target_compile_options(CrunchMB PRIVATE -Wall -Wextra)
endif()

# ---------- 离线断言测试（PLAN L2：独立 console target，不依赖 DAW） ----------
juce_add_console_app(CrunchMBTests PRODUCT_NAME "CrunchMBTests")
juce_generate_juce_header(CrunchMBTests)
target_sources(CrunchMBTests PRIVATE
    tests/TestMain.cpp
    src/Parameters.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/BandCompressor.cpp
    src/dsp/CrossoverBank.cpp
    src/dsp/LinearPhaseCrossover.cpp
    src/dsp/MultibandDSP.cpp
    src/dsp/SideChainFilter.h
    src/dsp/SpectrumAnalyzer.cpp
    src/dsp/SpectrumEngine.cpp
    src/ui/Anim.h
    src/ui/DisplayView.cpp
    src/ui/Knob.cpp)
target_include_directories(CrunchMBTests PRIVATE src)
target_compile_definitions(CrunchMBTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(CrunchMBTests PRIVATE
    CrunchMBData
    juce::juce_dsp
    juce::juce_audio_processors
    juce::juce_audio_utils)
if(MSVC)
    target_compile_options(CrunchMBTests PRIVATE /utf-8 /permissive-)
else()
    target_compile_options(CrunchMBTests PRIVATE -Wall -Wextra)
endif()


