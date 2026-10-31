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

# WebView2 SDK (YouTube の埋め込みに使う)。Microsoft の NuGet パッケージを、初回の構成時にだけ取得する
# 実行には WebView2 ランタイムが必要 (Windows 11 には標準で入っている)
set(WEBVIEW2_VERSION 1.0.1901.177)
set(WEBVIEW2_PACKAGES ${CMAKE_BINARY_DIR}/webview2/pkg)
set(WEBVIEW2_DIR ${WEBVIEW2_PACKAGES}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION})
if(NOT EXISTS ${WEBVIEW2_DIR}/build/native/include/WebView2.h)
    file(DOWNLOAD https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}
         ${CMAKE_BINARY_DIR}/webview2/webview2.nupkg
         EXPECTED_HASH SHA256=464360d5eedcae0e95b0a3fe13c74b496bc9b542980ff8ecebd9ff7ea37ca39e)
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/webview2/webview2.nupkg DESTINATION ${WEBVIEW2_DIR})
endif()
set(JUCE_WEBVIEW2_PACKAGE_LOCATION ${WEBVIEW2_PACKAGES})

juce_add_gui_app(GuitarPractice
    PRODUCT_NAME "Guitar Practice"
    COMPANY_NAME "GuitarPractice"
    NEEDS_WEBVIEW2 TRUE)

target_sources(GuitarPractice PRIVATE
    Source/Main.cpp
    Source/DevSnapshots.cpp
    Source/TimingLog.cpp
    Source/AppSettings.cpp
    Source/UI/SettingsPanel.cpp
    Source/Web/YouTubePlayer.cpp
    Source/Web/ScoreViewer.cpp
    Source/Web/WebResources.cpp
    Source/Audio/AudioEngine.cpp
    Source/Model/Session.cpp
    Source/Model/SessionStore.cpp
    Source/UI/MainComponent.cpp
    Source/UI/SessionListScreen.cpp
    Source/UI/SessionSetupScreen.cpp
    Source/UI/SessionScreen.cpp
    Source/UI/TunerPanel.cpp
    Source/Theme/ThemeManager.cpp
    Source/Theme/DeathMetalLookAndFeel.cpp
    Source/Theme/MetalLogo/MetalLogoGenerator.cpp
    Source/Theme/MetalLogo/MetalLogoCache.cpp
    Source/Theme/MetalLogo/MetalLogoLabel.cpp
    Source/Theme/MetalLogo/FixedTextButton.cpp
    Source/Theme/Dev/MetalLogoTuner.cpp)

# 実行ファイルに埋め込むファイル
#   フォント (OFL)            → BinaryData::DelaGothicOneRegular_ttf
#   YouTube のプレイヤーのページ → BinaryData::youtubeplayer_html
#   デザイントークンの CSS      → BinaryData::tokens_css (design/generate_tokens.py で作る)
#   タブ譜の表示 (alphaTab, MPL-2.0 / Bravura フォント, OFL) → BinaryData::scoreview_html, alphaTab_min_js, Bravura_woff2 ...
juce_add_binary_data(GuitarPracticeData
    SOURCES
        Resources/Fonts/DelaGothicOne-Regular.ttf
        Resources/Web/youtube-player.html
        Resources/Web/score-view.html
        Resources/Web/alphatab/alphaTab.min.js
        Resources/Web/alphatab/Bravura.woff2
        Resources/Web/alphatab/Bravura.woff
        design/generated/tokens.css)

target_compile_definitions(GuitarPractice PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
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
        GuitarPracticeData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
