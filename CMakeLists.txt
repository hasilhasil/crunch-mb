cmake_minimum_required(VERSION 3.24)
project(CrunchMB VERSION 1.0.4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE 8.0.4 内嵌源码树（兄弟项目共享；可 -DJUCE_DIR= 覆盖，独立克隆时回退 .cache/JUCE-8.0.4）
if(NOT DEFINED JUCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/JUCE-8.0.4/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/JUCE-8.0.4")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.cache/JUCE-8.0.4/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/JUCE-8.0.4")
    else()
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/JUCE-8.0.4")
    endif()
endif()

# 字体共享自兄弟项目（含空格路径，已验证可用；可 -DFONT_DIR= 覆盖，独立克隆时回退本仓或 .cache/Fonts）
if(NOT DEFINED FONT_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/src/ui/Fonts/Poppins-Regular.ttf")
        set(FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/src/ui/Fonts")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fonts/Poppins-Regular.ttf")
        set(FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fonts")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.cache/Fonts/Poppins-Regular.ttf")
        set(FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/Fonts")
    else()
        set(FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../260916 Crunch Compressor/src/ui/Fonts")
    endif()
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

# ---------- 字体二进制数据 ----------
set(FONT_SOURCES
    "${FONT_DIR}/Poppins-Regular.ttf"
    "${FONT_DIR}/Poppins-Medium.ttf"
    "${FONT_DIR}/Poppins-SemiBold.ttf"
    "${FONT_DIR}/Poppins-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fonts/Barlow-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fonts/Barlow-Medium.ttf")
juce_add_binary_data(CrunchMBData SOURCES ${FONT_SOURCES})

# ---------- 插件目标 ----------
juce_add_plugin(CrunchMB
    PRODUCT_NAME              "Crunch MB"
    COMPANY_NAME              "Kilo"
    BUNDLE_ID                 com.kilo.crunchmb
    PLUGIN_MANUFACTURER_CODE  Kilo
    # Fixed identity from 1.0.4 onward. Do not regenerate per build/version.
    PLUGIN_CODE               K7mB
    FORMATS                   VST3 Standalone
    VERSION                   1.0.4
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
    src/dsp/LoudnessMeter.cpp
    src/dsp/LoudnessMeter.h
    src/dsp/MultibandDSP.cpp
    src/dsp/SideChainFilter.h
    src/dsp/SpectrumAnalyzer.cpp
    src/dsp/SpectrumEngine.cpp
    src/ui/Anim.h
    src/ui/AnalyzerPanel.cpp
    src/ui/AnalyzerPanel.h
    src/ui/OutputPanel.cpp
    src/ui/OutputPanel.h
    src/api/IPluginBackend.h
    src/api/IMBBackend.h
    src/api/MBBackend.cpp
    src/api/MBBackend.h
    src/ui/BandPopover.cpp
    src/ui/BandPopover.h
    src/ui/DisplayView.cpp
    src/ui/FreqInputEditor.cpp
    src/ui/FreqInputEditor.h
    src/ui/Knob.cpp
    src/ui/LinkedDrawer.cpp
    src/ui/LinkedDrawer.h
    src/ui/SpectrumView.cpp
    src/ui/SpectrumView.h
    src/ui/core/PaletteContract.h
    src/ui/core/ThemeContract.h
    src/ui/core/LayoutContract.h
    src/ui/core/LocaleRuntime.h
    src/ui/core/NeuPainter.h
    src/ui/core/UiComposition.h
    src/ui/packs/palettes/BuiltInPalettes.h
    src/ui/packs/themes/BuiltInThemes.h
    src/ui/packs/layouts/BuiltInLayouts.h
    src/ui/packs/locales/BuiltInLocales.h)

target_compile_definitions(CrunchMB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # No VST2 edition is shipped. Do not advertise a legacy VST2 identity.
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# JUCE generates this manifest by querying the compiled binary's factory.
# Fail the build if a flag, product code or JUCE upgrade changes our identity.
if(WIN32)
    get_target_property(crunch_mb_vst3_bundle CrunchMB_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    add_custom_command(TARGET CrunchMB_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DMANIFEST=${crunch_mb_vst3_bundle}/Contents/Resources/moduleinfo.json"
            "-DEXPECTED_VERSION=1.0.4"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyVst3Identity.cmake"
        VERBATIM)
endif()

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
    src/dsp/LoudnessMeter.cpp
    src/dsp/LoudnessMeter.h
    src/dsp/MultibandDSP.cpp
    src/dsp/SideChainFilter.h
    src/dsp/SpectrumAnalyzer.cpp
    src/dsp/SpectrumEngine.cpp
    src/ui/Anim.h
    src/ui/AnalyzerPanel.cpp
    src/ui/AnalyzerPanel.h
    src/ui/OutputPanel.cpp
    src/ui/OutputPanel.h
    src/api/IPluginBackend.h
    src/api/IMBBackend.h
    src/api/MBBackend.cpp
    src/api/MBBackend.h
    src/ui/BandPopover.cpp
    src/ui/BandPopover.h
    src/ui/DisplayView.cpp
    src/ui/FreqInputEditor.cpp
    src/ui/FreqInputEditor.h
    src/ui/Knob.cpp
    src/ui/LinkedDrawer.cpp
    src/ui/LinkedDrawer.h
    src/ui/SpectrumView.cpp
    src/ui/SpectrumView.h
    src/ui/core/PaletteContract.h
    src/ui/core/ThemeContract.h
    src/ui/core/LayoutContract.h
    src/ui/core/LocaleRuntime.h
    src/ui/core/NeuPainter.h
    src/ui/core/UiComposition.h
    src/ui/packs/palettes/BuiltInPalettes.h
    src/ui/packs/themes/BuiltInThemes.h
    src/ui/packs/layouts/BuiltInLayouts.h
    src/ui/packs/locales/BuiltInLocales.h)
target_include_directories(CrunchMBTests PRIVATE src)
target_compile_definitions(CrunchMBTests PRIVATE
    JUCE_PLUGINHOST_VST3=1
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


