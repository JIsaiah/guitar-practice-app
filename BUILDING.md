# ビルド方法

必要なもの: Visual Studio 2022 Build Tools (C++ ワークロード)、CMake 3.22 以上。JUCE は初回の構成時に自動で取得されます。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

実行ファイル: `build\GuitarPractice_artefacts\Debug\Guitar Practice.exe`

- セッションの保存先: `ドキュメント\GuitarPractice\Sessions\*.gppractice`
- オーディオ設定の保存先: `%APPDATA%\GuitarPractice\GuitarPractice.settings`
- VC++ ランタイムは静的リンクしているため、配布先に追加のインストールは不要です

## ソースの構成

| パス | 役割 |
| --- | --- |
| `Source/Main.cpp` | アプリの起動・終了、ウィンドウ |
| `Source/Audio/AudioEngine` | 音声処理 (ギター入力→音色、ローカル音源、メトロノーム、3系統ミキサー、チューナー入力) |
| `Source/Audio/TapTempo.h`, `PitchDetector.h` | タップテンポ、音程検出 |
| `Source/Model/Session`, `SessionStore` | セッションのデータと `.gppractice` の読み書き |
| `Source/UI/MainComponent` | 画面遷移 (一覧 → 作成 → セッション → 一覧) |
| `Source/UI/SessionListScreen` | セッション一覧 |
| `Source/UI/SessionSetupScreen` | 新しいセッションの作成 |
| `Source/UI/SessionScreen` | 練習画面。操作はすべて `Commands.h` のコマンド経由 |
| `Source/UI/TunerPanel` | チューナー (セッション画面の上に重ねる) |
