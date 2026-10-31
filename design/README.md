# design/ — 見た目の正本

設計書 v0.7 の 4.4〜4.9、実装指示 T1・T2 にもとづくフォルダです。

| パス | 内容 |
| --- | --- |
| `tokens.json` | **デザイントークンの正本**。色・文字・余白・角の丸み・影・動き。テーマ（`light`、`dark`）ごとの色 |
| `generate_tokens.py` | `tokens.json` から下の3つを作るスクリプト |
| `generated/tokens.css` | CSS の変数（試作の HTML と、アプリの YouTube のページが読む） |
| `generated/contrast.md` | 文字と背景のコントラスト比の一覧 |
| `../Source/UI/DesignTokens.h` | C++ の定数（JUCE の画面が使う） |
| `prototype/` | 4画面の HTML の試作（Impeccable で見た目を決める対象） |

## トークンを変えたとき

```powershell
python design/generate_tokens.py
```

- 生成物（`generated/` と `DesignTokens.h`）はコミットします。ビルド時の自動生成にはしていません（Python がなくてもビルドできるように）
- スクリプトは、作った CSS と C++ を読み直して **色の値が一致しているか** と **コントラスト比が基準以上か** を確かめ、問題があれば終了コード 1 で終わります
- `tokens.css` はアプリにも埋め込んでいるので、変えたらアプリもビルドし直してください
- テーマを増やすときは、`tokens.json` の `themes` に組を1つ足すだけです（C++ では `DesignTokens::allThemes` に自動で入ります）

## 試作を見る

`prototype/session-list.html` をブラウザで開きます（ネット接続は不要）。画面上部の小さなボタンで「OS に合わせる → ライト → ダーク」を切り替えられます。URL に `?theme=dark` を付けても指定できます。

| ファイル | 画面 |
| --- | --- |
| `session-list.html` | セッション一覧（起動画面）。カードのフェードイン |
| `practice.html` | 練習画面 |
| `new-session.html` | 新しいセッション |
| `settings.html` | 設定 |

### 守っている制約（実装指示 2.1。JUCE に移植できる表現だけ）
- レイアウトは flex と grid。影は `--shadow` の1つだけ。角の丸みは単純なもの
- `backdrop-filter`、複雑なグラデーション、CSS の `filter` は使わない
- 動きは `opacity` と `transform` だけ（0.18秒）。Windows の「アニメーション効果」がオフ（`prefers-reduced-motion`）なら動かさない
- 真っ白・真っ黒は使わない。フォント・スクリプト・画像はすべて同梱（ネットから読まない）
