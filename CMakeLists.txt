cmake_minimum_required(VERSION 3.22)

project(GuitarPractice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配布先に VC++ ランタイムを入れなくて済むよう、ランタイムを静的リンクする
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(GuitarPractice
    PRODUCT_NAME "Guitar Practice"
    COMPANY_NAME "GuitarPractice")

target_sources(GuitarPractice PRIVATE
    Source/Main.cpp
    Source/Audio/AudioEngine.cpp
    Source/Model/Session.cpp
    Source/Model/SessionStore.cpp
    Source/UI/MainComponent.cpp
    Source/UI/SessionListScreen.cpp
    Source/UI/SessionSetupScreen.cpp
    Source/UI/SessionScreen.cpp
    Source/UI/TunerPanel.cpp)

target_compile_definitions(GuitarPractice PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GuitarPractice,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GuitarPractice,JUCE_VERSION>")

if(MSVC)
    # ソースは UTF-8 (日本語の文字列リテラルを含む)
    target_compile_options(GuitarPractice PRIVATE /utf-8)
endif()

target_link_libraries(GuitarPractice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
