# ビルド方法

起動後の使い方は `クイックスタート.md` を見てください。

必要なもの: Visual Studio 2022 Build Tools (C++ ワークロード)、CMake 3.22 以上。JUCE は初回の構成時に自動で取得されます。

WebView2 SDK (YouTube の埋め込みに使う) も、初回の構成時に `build\webview2\` へ自動で取得されます (NuGet から。ハッシュで確認)。実行には WebView2 ランタイムが必要です (Windows 11 には標準で入っています)。

ASIO SDK (Steinberg。GPL v3 との二重ライセンス) も、初回の構成時に公式サイトから `build\asiosdk\` へ自動で取得されます (ハッシュで確認)。使うのはヘッダーだけです。

NeuralAmpModelerCore v0.6.0 (MIT。NAM の音色) と、その依存の Eigen (MPL-2.0)・nlohmann/json (MIT) も、初回の構成時に自動で取得されます。NAM の部分は Debug ビルドでも最適化してコンパイルします (Eigen は最適化しないとリアルタイムに間に合わないため)。

開発用の確認 (Debug のみ): `"Guitar Practice.exe" --audio-check <出力フォルダ> --nam <モデル.nam> [--ir <IR.wav>]` で、使えるオーディオデバイスの一覧と、NAM の処理時間を `audio-check.txt` に書き出します (音は出しません)。

デザイントークン (`design/tokens.json`) を変えたときは、ビルドの前に `python design/generate_tokens.py` を実行します (`design/README.md`)。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

実行ファイル: `build\GuitarPractice_artefacts\Debug\Guitar Practice.exe`

- セッションの保存先: `ドキュメント\GuitarPractice\Sessions\*.gppractice`
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
| `Source/DevAudioCheck` | 開発用 (Debug のみ): `--audio-check` でデバイスの一覧と NAM の処理時間を確かめる |
| `Source/Audio/TapTempo.h`, `PitchDetector.h` | タップテンポ、音程検出 |
| `Source/Model/Session`, `SessionStore` | セッションのデータと `.gppractice` の読み書き |
| `Source/UI/MainComponent` | 画面遷移 (一覧 → 作成 → セッション → 一覧) |
| `Source/UI/SessionListScreen` | セッション一覧 |
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
