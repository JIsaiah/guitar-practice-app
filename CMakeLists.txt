cmake_minimum_required(VERSION 3.22)

project(GuitarPractice VERSION 1.0.1 LANGUAGES C CXX)

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

# ASIO SDK (Steinberg)。ASIO ドライバーで、低い遅延でギターの音を出すのに使う (設計書 #13、PoC-1)
# ライセンスは Steinberg の独自ライセンスと GPL v3 の二重ライセンス。このアプリ (AGPL v3) では GPL v3 を選ぶ
# 公式サイトの ZIP を、初回の構成時にだけ取得する (ハッシュで確認)。使うのはヘッダー (common/) だけ
set(ASIOSDK_DIR ${CMAKE_BINARY_DIR}/asiosdk/ASIOSDK)
if(NOT EXISTS ${ASIOSDK_DIR}/common/iasiodrv.h)
    file(DOWNLOAD https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
         ${CMAKE_BINARY_DIR}/asiosdk/asiosdk.zip
         EXPECTED_HASH SHA256=D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA)
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/asiosdk/asiosdk.zip DESTINATION ${CMAKE_BINARY_DIR}/asiosdk)
endif()

# NeuralAmpModelerCore (MIT)。NAM のアンプのモデル (.nam) と、キャビネットの IR (.wav) を動かす
# 依存: Eigen (MPL-2.0。サブモジュールとして取得) と nlohmann/json (MIT。リポジトリに同梱)
# SOURCE_SUBDIR に存在しないフォルダを指定して、取得だけ行う (付属のツールやテストはビルドしない)
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "Dependencies/eigen"
    SOURCE_SUBDIR  do-not-build)
FetchContent_MakeAvailable(NeuralAmpModelerCore)

set(NAM_DIR ${neuralampmodelercore_SOURCE_DIR})
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/wavenet/*.cpp)
# OBJECT ライブラリにする (STATIC だと動かない): NAM はモデルの種類 (WaveNet など) を、起動時に自分で一覧へ登録する。
# 静的ライブラリにすると、どこからも直接呼ばれない登録のコードをリンカーが捨て、「WaveNet を読めない」エラーになる
add_library(nam OBJECT ${NAM_SOURCES})
target_include_directories(nam SYSTEM PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/NAM
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann)
target_compile_definitions(nam PUBLIC
    NAM_SAMPLE_FLOAT     # 音のデータを float で受け渡す (JUCE と同じ)
    EIGEN_NO_DEBUG       # Eigen の確認処理を外す (オーディオスレッドで重くならないように)
    NOMINMAX)
target_compile_features(nam PUBLIC cxx_std_20)
if(MSVC)
    # Eigen は最適化しないと極端に遅く、リアルタイムに間に合わないため、Debug ビルドでも最適化する
    # (/O2 が Debug の /Od を上書きする。コマンドラインの警告 D9025 が出るが問題ない)
    target_compile_options(nam PRIVATE /utf-8 /bigobj $<$<CONFIG:Debug>:/O2 /Ob2>)
    # /O2 と、Debug の実行時チェック (/RTC1) は同時に使えない。プロジェクト全体の Debug から /RTC1 を外す
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

juce_add_gui_app(GuitarPractice
    PRODUCT_NAME "suguhiku"   # アプリの名前 (ウィンドウの題名・実行ファイル名)。保存先のフォルダ名 (GuitarPractice) は変えない
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
    Source/Audio/NamTone.cpp
    Source/Audio/PluginTone.cpp
    Source/UI/PluginToneCard.cpp
    Source/Platform/WindowOwner.cpp
    Source/Platform/CrashLog.cpp
    Source/Platform/MouseState.cpp
    Source/UI/RadialMenu.cpp
    Source/UI/DebugLogPanel.cpp
    Source/UI/SessionFolderList.cpp
    Source/DevAudioCheck.cpp
    Source/UI/ToneDetailPanel.cpp
    Source/Model/Session.cpp
    Source/Model/SessionStore.cpp
    Source/Library/SessionLibrary.cpp
    Source/Library/Setlist.cpp
    Source/Library/SetlistStore.cpp
    Source/Session/PlaybackSettingsResolver.cpp
    Source/Setlist/CountdownController.cpp
    Source/Setlist/SetlistPlayer.cpp
    Source/UI/MainComponent.cpp
    Source/UI/SessionListScreen.cpp
    Source/UI/SetlistPanel.cpp
    Source/UI/SetlistCard.cpp
    Source/UI/SetlistEntryList.cpp
    Source/UI/SessionPickerPanel.cpp
    Source/UI/SessionSetupScreen.cpp
    Source/UI/SessionScreen.cpp
    Source/UI/CountdownOverlay.cpp
    Source/UI/Icons.cpp
    Source/UI/SetlistBar.cpp
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
        Resources/Web/alphatab/sonivox.sf2
        design/generated/tokens.css
        # 「歪み」の音色 (作者が選んだ NAM のモデルとキャビネットの IR。元は 音色プリセット/ のファイル)
        Resources/Tones/drive-amp.nam
        Resources/Tones/drive-cab.wav
        # アイコン (作者が置いた SVG。Source/UI/Icons で描く)
        "svg icons/help-circle-svgrepo-com.svg"
        "svg icons/media-backward-end-svgrepo-com.svg"
        "svg icons/media-backward-svgrepo-com.svg"
        "svg icons/media-forward-svgrepo-com.svg"
        "svg icons/media-play-svgrepo-com.svg"
        "svg icons/media-pause-svgrepo-com.svg"
        "svg icons/plus-svgrepo-com.svg"
        "svg icons/setting-2-svgrepo-com.svg"
        "svg icons/trash-svgrepo-com.svg"
        "svg icons/sort-sortable-anchor-table-order-desc-asc-svgrepo-com.svg"
        "svg icons/sort-asc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg"
        "svg icons/sort-desc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg")

target_compile_definitions(GuitarPractice PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_ASIO=1
    JUCE_PLUGINHOST_VST3=1   # VST3 プラグイン (TONE3000 など) を鳴らす
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GuitarPractice,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GuitarPractice,JUCE_VERSION>")

if(MSVC)
    # ソースは UTF-8 (日本語の文字列リテラルを含む)
    target_compile_options(GuitarPractice PRIVATE /utf-8)
endif()

# ASIO SDK のヘッダー (JUCE が iasiodrv.h を読む)
target_include_directories(GuitarPractice PRIVATE ${ASIOSDK_DIR}/common)

target_link_libraries(GuitarPractice
    PRIVATE
        nam
        GuitarPracticeData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# 自動テスト (JUCE UnitTest。setlist-mode.md 10章)。音や画面に関係しない部分だけを、コンソールのアプリでテストする
#   cmake --build build --config Debug --target GuitarPracticeTests
#   ctest --test-dir build -C Debug --output-on-failure
enable_testing()
juce_add_console_app(GuitarPracticeTests
    PRODUCT_NAME "suguhiku Tests")

target_sources(GuitarPracticeTests PRIVATE
    Tests/NoiseGateTests.cpp
    Tests/TestMain.cpp
    Tests/SetlistTests.cpp
    Tests/PlaybackSettingsResolverTests.cpp
    Tests/CountdownControllerTests.cpp
    Tests/SetlistPlayerTests.cpp
    Tests/MusicInfoTests.cpp
    Tests/PitchDetectorTests.cpp
    Tests/SessionFolderTests.cpp
    Tests/SessionSortTests.cpp
    Tests/ScorePartsTests.cpp
    Tests/ScoreClockTests.cpp
    Source/Model/Session.cpp
    Source/Model/SessionStore.cpp
    Source/Library/SessionLibrary.cpp
    Source/Library/Setlist.cpp
    Source/Library/SetlistStore.cpp
    Source/Session/PlaybackSettingsResolver.cpp
    Source/Setlist/CountdownController.cpp
    Source/Setlist/SetlistPlayer.cpp)

target_compile_definitions(GuitarPracticeTests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_UNIT_TESTS=0)   # JUCE 自身のテストは入れない (このアプリのテストだけを実行する)

if(MSVC)
    target_compile_options(GuitarPracticeTests PRIVATE /utf-8)
endif()

target_link_libraries(GuitarPracticeTests
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME GuitarPracticeTests COMMAND GuitarPracticeTests)
