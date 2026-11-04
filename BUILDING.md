# ビルド方法

起動後の使い方は `クイックスタート.md` を見てください。

必要なもの: Visual Studio 2022 Build Tools (C++ ワークロード)、CMake 3.22 以上。JUCE は初回の構成時に自動で取得されます。

WebView2 SDK (YouTube の埋め込みに使う) も、初回の構成時に `build\webview2\` へ自動で取得されます (NuGet から。ハッシュで確認)。実行には WebView2 ランタイムが必要です (Windows 11 には標準で入っています)。

ASIO SDK (Steinberg。GPL v3 との二重ライセンス) も、初回の構成時に公式サイトから `build\asiosdk\` へ自動で取得されます (ハッシュで確認)。使うのはヘッダーだけです。

NeuralAmpModelerCore v0.6.0 (MIT。NAM の音色) と、その依存の Eigen (MPL-2.0)・nlohmann/json (MIT) も、初回の構成時に自動で取得されます。NAM の部分は Debug ビルドでも最適化してコンパイルします (Eigen は最適化しないとリアルタイムに間に合わないため)。

開発用の確認 (Debug のみ): `"Guitar Practice.exe" --audio-check <出力フォルダ> --nam <モデル.nam> [--ir <IR.wav>] [--plugin <プラグイン.vst3>]` で、使えるオーディオデバイスの一覧、NAM の処理時間、VST3 プラグインの読み込み・処理時間・状態の保存と復元・画面の大きさを `audio-check.txt` に書き出します (音は出さず、画面も開きません)。

`"Guitar Practice.exe" --plugin-window-test <出力フォルダ> --plugin <プラグイン.vst3>` は、練習画面と同じ部品でプラグインの画面のウィンドウを開き、約 10 秒後に、Windows から見た表示の状態 (表示・位置・持ち主・プラグインが描く子ウィンドウ) を `plugin-window.txt` に書き出して終わります (画面に数秒ウィンドウが出ます。画面の撮影はしません)。

デザイントークン (`design/tokens.json`) を変えたときは、ビルドの前に `python design/generate_tokens.py` を実行します (`design/README.md`)。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

実行ファイル: `build\GuitarPractice_artefacts\Debug\Guitar Practice.exe`

- セッションの保存先: `ドキュメント\GuitarPractice\Sessions\*.gppractice`
- セットリストの保存先: `ドキュメント\GuitarPractice\Setlists\*.gpsetlist`
- 自動テスト: `cmake --build build --config Debug --target GuitarPracticeTests` のあと `ctest --test-dir build -C Debug --output-on-failure`
- アプリの設定の保存先: `%APPDATA%\GuitarPractice\GuitarPractice.settings` (オーディオ設定、デスメタルモード)
- 時間の計測の記録: `%APPDATA%\GuitarPractice\timing.log`。WebView のデータ: `%APPDATA%\GuitarPractice\WebView2\`
- VC++ ランタイムは静的リンクしているため、配布先に追加のインストールは不要です

## ソースの構成

| パス | 役割 |
| --- | --- |
| `Source/Main.cpp` | アプリの起動・終了、ウィンドウ |
| `Source/Audio/AudioEngine` | 音声処理 (ギター入力→音色、ローカル音源、メトロノーム、3系統ミキサー、チューナー入力) |
| `Source/Audio/NamTone` | NAM の音色 (アンプのモデル .nam + キャビネットの IR .wav) |
| `Source/UI/ToneDetailPanel` | 音色の設定 (NAM のファイル、入力の大きさ、メーター、デバイスの情報) |
| `Source/Audio/PluginTone`, `PluginLocator.h` | VST3 プラグインの音色 (TONE3000 など)。標準の置き場所から名前で探す |
| `Source/UI/PluginToneCard` | TONE3000 のカード (今はプラグインの画面を別のウィンドウで開く) |
| `Source/Platform/WindowOwner` | Windows の「所有ウィンドウ」の設定と、ウィンドウの表示の状態の確認 (windows.h を使う部分だけを分けた) |
| `Source/DevAudioCheck` | 開発用 (Debug のみ): `--audio-check` でデバイスの一覧と NAM の処理時間を確かめる |
| `Source/Audio/TapTempo.h`, `PitchDetector.h` | タップテンポ、音程検出 |
| `Source/Model/Session`, `SessionStore` | セッションのデータと `.gppractice` の読み書き |
| `Source/UI/MainComponent` | 画面遷移 (一覧 → 作成 → セッション → 一覧) |
| `Source/UI/SessionListScreen` | 一覧 (タブ: セッション／セットリスト) |
| `Source/Library/` | セッション ID → ファイルの対応表 (`SessionLibrary`)、セットリストのデータと `.gpsetlist` の読み書き (`Setlist`, `SetlistStore`) |
| `Source/UI/SetlistPanel`, `SetlistCard`, `SetlistEntryList`, `SessionPickerPanel` | セットリストのタブ (カード、曲順の編集、曲の追加パネル)。`Claudeこれ見て/setlist-mode.md` |
| `Tests/` | 自動テスト (JUCE UnitTest)。音や画面に関係しない部分 |
| `Source/UI/SessionSetupScreen` | 新しいセッションの作成 |
| `Source/UI/SessionScreen` | 練習画面。操作はすべて `Commands.h` のコマンド経由 |
| `Source/UI/TunerPanel` | チューナー (セッション画面の上に重ねる) |
| `Source/Theme/` | 見た目のモード (通常／デスメタル)、ロゴの生成。`death-metal-mode.md` |
| `Source/DevSnapshots` | 開発用 (Debug のみ): `--render-logos <フォルダ>` で画面とロゴを PNG に書き出す |
| `Source/Web/YouTubePlayer` | YouTube の埋め込みプレイヤー (WebView2)。一覧の表示後に裏で準備し、練習画面で使い回す |
| `Source/TimingLog` | 起動・画面遷移の時間の記録 |
| `Resources/Web/` | YouTube のプレイヤーのページ (実行ファイルに埋め込み、`https://juce.backend/` で開く) |
| `design/` | デザイントークンと HTML の試作 (`design/README.md`) |
| `Resources/Fonts/` | Dela Gothic One (SIL Open Font License。`OFL.txt`)。実行ファイルに埋め込む |
