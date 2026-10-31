# デスメタルモード 実装仕様書

> おまけ機能（冗談枠）。MVPの機能には影響させないこと。
> この文書はClaude Codeに実装を依頼するための仕様書です。上から順に「フェーズ」単位で実装し、各フェーズの完了条件を満たしてから次に進んでください。

## 0. 前提

- 言語・フレームワーク：C++20 / JUCE 8
- 対象OS：Windowsのみ
- ビルド：CMake（`juce_add_gui_app` / `juce_add_binary_data`）を想定。Projucer構成なら同等の手順に読み替える
- ライセンス：アプリはAGPLv3。同梱フォントはSIL Open Font License（OFL）のもののみ使う
- 優先事項：低遅延・UIの反応の速さ。**ロゴ生成をオーディオスレッドで行ってはならない**。`paint()`内でも生成しない（キャッシュ画像を描くだけ）

## 1. 機能概要

設定のトグル（または隠しコマンド）でデスメタルモードに切り替えると、次のように変わる。

| 対象 | 通常モード | デスメタルモード |
|---|---|---|
| 配色 | 通常テーマ | 黒・暗赤・灰の暗い配色 |
| 決まった文言（アプリ名、ボタン名、見出しなど） | 通常フォント | **手続き的に生成したデスメタルロゴ風の画像** |
| ユーザーが入力するテキスト（曲名、セッション名など） | 通常フォント | **Google Fontsの日本語フォント（OFL）で普通に描画** |

### ロゴ風の表現（決まった文言のみ）

1. **はらい・はねの延長**：文字の線の端（鋭い角）を外側に引き伸ばす
2. **トゲ**：文字の上端・下端付近の輪郭から、上下方向にトゲを生やす
3. **密度の問題は今回は扱わない**。代わりに、ロゴ描画時は**文字サイズと文字間隔を大きく取る**ことで、トゲ同士・文字同士が潰れないようにする

### スコープ外（今回はやらない）

- 画数に応じたトゲ密度の自動調整
- ロゴ全体の左右対称化
- 手描きSVGロゴ
- ユーザー入力テキストのロゴ化

## 2. フォント

| 用途 | フォント | 備考 |
|---|---|---|
| ロゴの土台（決まった文言） | Dela Gothic One | 極太なので、延長・トゲを付けても形が崩れにくい |
| ユーザー入力テキスト | Dela Gothic One（同じファイル） | 1ファイルで両用し、バイナリサイズを抑える |

- フォントファイルは `Resources/Fonts/DelaGothicOne-Regular.ttf` に置き、`juce_add_binary_data` で埋め込む
- `OFL.txt` を同じフォルダに置き、配布物にも含める。「このアプリについて」画面にフォント名とOFLの表記を出す
- フォントに無い文字（絵文字、珍しい漢字など）は、JUCEのフォントフォールバックでシステムフォントに落とす。文字化けや豆腐（□）が出ないこと
- ※ユーザー入力用のフォントを別のもの（例：RocknRoll One）に変えたくなった場合に備えて、フォントの読み込みは`ThemeManager`の1か所に集約する

## 3. ファイル構成

```
Source/
  Theme/
    ThemeManager.h / .cpp           // モード状態の管理・保存・変更通知
    Palette.h                       // 配色の定義（通常/デスメタル）
    DeathMetalLookAndFeel.h / .cpp  // 配色とユーザー入力用フォントの差し替え
    MetalLogo/
      MetalLogoParams.h             // 生成パラメータ（調整用の値をすべてここに集約）
      MetalLogoGenerator.h / .cpp   // 文字列 → Path の生成（純粋関数・UI非依存）
      MetalLogoCache.h / .cpp       // 生成結果を Image にキャッシュ
      MetalLogoLabel.h / .cpp       // ロゴを表示するComponent
      FixedTexts.h                  // ロゴ化する決まった文言の一覧
    Dev/
      MetalLogoTuner.h / .cpp       // 開発用：パラメータ調整ウィンドウ（Debugビルドのみ）
Tests/
  MetalLogoGeneratorTests.cpp
Resources/
  Fonts/
    DelaGothicOne-Regular.ttf
    OFL.txt
```

## 4. 各モジュールの仕様

### 4.1 ThemeManager

- `enum class ThemeMode { Normal, DeathMetal };`
- `juce::ChangeBroadcaster` を継承し、モード変更時に `sendChangeMessage()` する
- モードはアプリ全体の設定として `juce::PropertiesFile` に保存する（**セッション（.gppractice）には保存しない**）
- フォント（`juce::Typeface::Ptr`）の読み込みはここで1回だけ行い、他のモジュールに渡す
- モード切替時の処理：
  1. トップレベルのComponentに`setLookAndFeel()`
  2. `sendLookAndFeelChange()`で子に伝える
  3. `MetalLogoCache`を無効化（再生成させる）

### 4.2 Palette / DeathMetalLookAndFeel

- `LookAndFeel_V4`を継承
- 配色の目安（調整可）：
  - 背景 `#0A0A0A`、パネル `#141414`
  - ボタン `#1A0000`、ボタン押下 `#3A0000`
  - 文字 `#B0B0B0`、強調 `#8B0000`
  - スライダーのつまみ・フェーダー `#8B0000`
- ユーザー入力テキストを描く箇所（`Label`、`TextEditor`、一覧の行など）のフォントを Dela Gothic One に差し替える
- 決まった文言のボタンは、デスメタルモード時は`MetalLogoLabel`で描く（4.6参照）
- **ヒットボックスの大きさは変えない**（片手・マウス操作のコンセプトを崩さない）

### 4.3 MetalLogoParams

調整したい値はすべてここに置く。マジックナンバーをコードに直書きしないこと。

```cpp
struct MetalLogoParams
{
    // レイアウト（密度対策：大きめに取る）
    float fontHeight      = 96.0f;   // ロゴ生成時の基準サイズ（px）。表示時に縮小する
    float tracking        = 0.35f;   // 文字間隔（fontHeight に対する比率）
    float margin          = 0.6f;    // 上下左右の余白（fontHeight 比）。トゲがはみ出さないように

    // 輪郭の分割
    float flattenTolerance = 0.5f;   // PathFlatteningIterator の許容誤差

    // はらい・はねの延長
    float tipAngleDeg      = 110.0f; // この角度以上に鋭く曲がる凸の角を「線の端」とみなす
    float tipExtend        = 0.25f;  // 延長量（fontHeight 比）
    int   tipFalloffVerts  = 3;      // 前後何頂点まで減衰させながら引っ張るか
    float tipJitter        = 0.3f;   // 延長量のランダム幅（0〜1）

    // トゲ
    float spikeBand        = 0.15f;  // グリフ上下の何割を「外周帯」とみなすか
    float spikeLenMin      = 0.12f;  // トゲ長さ（fontHeight 比）
    float spikeLenMax      = 0.35f;
    float spikeBaseWidth   = 0.04f;  // トゲ根元の幅（fontHeight 比）
    float spikeMinGap      = 0.08f;  // トゲ同士の最小間隔（fontHeight 比）
    float spikeProbability = 0.5f;   // 候補点にトゲを生やす確率
    int   collisionSamples = 5;      // 衝突判定のサンプル数
};
```

### 4.4 MetalLogoGenerator（中核）

```cpp
class MetalLogoGenerator
{
public:
    // 文字列から、延長・トゲ付きのPathを生成する。
    // 同じ text / params / seed なら必ず同じ結果を返すこと（決定的）。
    static juce::Path generate (const juce::String& text,
                                const juce::Font& baseFont,
                                const MetalLogoParams& params,
                                juce::uint32 seed);
};
```

- UIにもスレッドにも依存しない純粋な処理にする（単体テストできるように）
- 乱数は `juce::Random (seed)` を使う。seedは呼び出し側で `text.hashCode()` から作る

#### 処理手順

**Step 1：レイアウト**
- `baseFont` を `params.fontHeight` の高さにする
- `GlyphArrangement::addLineOfText()` で並べたあと、各グリフに `i * fontHeight * tracking` の追加オフセットを与えて文字間隔を広げる
- 各グリフについて `PositionedGlyph::createPath()` で個別の `Path` を取り出す（**以降の処理はすべてグリフ単位**）
- 空白などPathが空のグリフは飛ばす

**Step 2：輪郭を頂点列に変換**
- `juce::PathFlatteningIterator` でグリフのPathを折れ線にし、サブパス（輪郭）ごとの頂点列 `std::vector<std::vector<Point<float>>>` にする
- 連続する重複頂点、ほぼ同一直線上の頂点は間引く

**Step 3：はらい・はねの延長**
- 各輪郭の各頂点 `v[i]` について、前後の頂点 `v[i-1]`、`v[i+1]` から曲がる角度を求める
- 次の条件をすべて満たす頂点を「線の端」とする：
  - 曲がる角度が `tipAngleDeg` 以上（鋭い角）
  - **凸の角である**：角の二等分線の外向き方向に少し進んだ点が、元のグリフPathに対して `contains() == false`
- 「線の端」の頂点を、二等分線の外向き方向に `fontHeight * tipExtend * (1 ± tipJitter)` だけ移動する
- 前後 `tipFalloffVerts` 個の頂点も、距離に応じて減衰させながら同じ方向に移動する（先細りの形を保つため）
- 移動後に輪郭が自己交差しないよう、移動量は隣の頂点との距離の数倍までに制限する

**Step 4：トゲ**
- 延長後の輪郭からグリフのPathを作り直し、その外接矩形 `b` を求める
- 輪郭上を一定間隔でたどり、次の条件をすべて満たす点 `p` にトゲを生やす：
  1. `p` がグリフの上端帯（`b.getY() + b.getHeight() * spikeBand` より上）または下端帯にある
  2. 確率 `spikeProbability` で当選
  3. 直前に生やしたトゲから `spikeMinGap` 以上離れている
  4. 向き `dir` は上端帯なら上 `(0,-1)`、下端帯なら下 `(0,1)`
  5. **外向き判定**：`p + dir * 1.5f` がグリフに含まれない
  6. **衝突判定**：`p` から先端までを `collisionSamples` 個に分けて調べ、どの点も**テキスト全体のPath**に含まれない（隣の文字にも刺さらない）
  7. **先端が外接矩形の外に出る**：上向きなら先端の y < `b.getY()`、下向きなら先端の y > `b.getBottom()`（「口」のような字の内側の穴にトゲが入るのを防ぐ）
- トゲは三角形：根元は `p` を中心に輪郭の接線方向へ `±spikeBaseWidth/2`、先端は `p + dir * 長さ`
- 長さは `spikeLenMin`〜`spikeLenMax` の間でランダム

**Step 5：合成**
- 延長後の全グリフの輪郭と全トゲを1つの `juce::Path` にまとめる
- `setUsingNonZeroWinding (true)` にして、トゲと本体が合体して塗られるようにする
- 結果のPathを、余白 `margin` 込みで原点基準に平行移動して返す

### 4.5 MetalLogoCache

- キー：`(text, 表示サイズ(px), 表示スケール(DPI), パラメータのハッシュ)`
- 値：`juce::Image`（ARGB、透過背景、文字色はPaletteから）
- 表示サイズに合わせて `generate()` の結果をスケーリングして描き込む。高DPI対応のため、`Component::getApproximateScaleFactorForComponent()` 倍の解像度で作る
- 生成は**モード切替時・ウィンドウサイズ変更時にだけ**行う
- 計測して1回の生成が16msを超えるようなら、`juce::ThreadPool` でバックグラウンド生成し、完成したら `MessageManager::callAsync` で該当Componentを `repaint()` する。生成中は通常フォントで仮表示する
- モード切替で通常モードに戻ったらキャッシュを破棄する

### 4.6 MetalLogoLabel / FixedTexts

- `FixedTexts.h` にロゴ化する決まった文言を一覧で定義する（ここに無い文字列はロゴ化しない）

```cpp
enum class FixedTextId { AppTitle, DeathMetalMode, Play, Stop, Tuner, Metronome, Sessions, Settings /* … */ };
juce::String getFixedText (FixedTextId id); // 例: Play → "再生"
```

- `MetalLogoLabel` は `FixedTextId` を受け取り、通常モードでは通常フォントで、デスメタルモードでは `MetalLogoCache` の画像を描く
- 画像はComponentの枠内にアスペクト比を保って収める（`RectanglePlacement::centred`）
- アクセシビリティとツールチップには元の文字列をそのまま設定する（ロゴが読めなくても何のボタンかわかるように）

### 4.7 切り替え方法

- 設定画面のトグル「デスメタルモード」
- 隠しコマンド：アプリ名のロゴを**6回連続クリック**（間隔1秒以内）で切り替え
- どちらもマウスだけで操作でき、キーボード入力は不要

### 4.8 MetalLogoTuner（開発用、Debugビルドのみ）

- `MetalLogoParams` の各値をスライダーで動かし、プレビューをリアルタイムに更新するウィンドウ
- 入力欄に任意の文字列を入れて試せる（このツール内に限りユーザー入力もロゴ化してよい）
- 「パラメータをクリップボードにコピー」ボタンで、現在の値をC++の初期化子の形で出力する
- `#if JUCE_DEBUG` で囲み、Releaseビルドには含めない

## 5. テスト（JUCE UnitTest）

`MetalLogoGeneratorTests.cpp` に次を実装する。

1. **決定性**：同じ text / params / seed で2回生成したPathが一致する
2. **seedの違い**：seedを変えると結果が変わる
3. **空文字・空白のみ**：クラッシュせず、空のPathを返す
4. **全固定文言**：`FixedTextId` のすべてで生成でき、Pathが空でない
5. **はみ出し**：生成結果の外接矩形が、余白込みの想定範囲に収まる
6. **トゲの存在**：「デスメタルモード」で、元のグリフより外接矩形が上下に広がっている
7. **フォールバック**：フォントに無い文字を含んでもクラッシュしない

## 6. 実装フェーズ

| フェーズ | 内容 | 完了条件 |
|---|---|---|
| 1 | ThemeManager、Palette、DeathMetalLookAndFeel、フォント埋め込み、設定トグル | トグルで配色とユーザー入力テキストのフォントが切り替わり、再起動後も保持される |
| 2 | MetalLogoGenerator の Step 1・2・5（延長もトゲも無し）、MetalLogoLabel、MetalLogoCache | 決まった文言が大きな文字間隔で、Pathから描いた画像として表示される |
| 3 | Step 4（トゲ） | 上下にトゲが生え、隣の文字や字の内側の穴に刺さらない |
| 4 | Step 3（はらい・はねの延長） | 「デ」「ス」「メ」「タ」「ル」「再」「生」などで線の端が伸び、輪郭が破綻しない |
| 5 | MetalLogoTuner、パラメータ調整 | Tunerで値を決め、`MetalLogoParams` の初期値に反映 |
| 6 | 隠しコマンド、テスト、OFL表記 | テストがすべて通り、6回クリックで切り替わり、「このアプリについて」にフォント表記がある |

**フェーズ3をフェーズ4より先にする理由**：トゲは外接矩形と`contains()`だけで実装でき、延長より単純なため。先にパイプライン全体を動かし、難しい延長は後から差し込む。

## 7. 注意事項

- 生成・キャッシュ処理はメッセージスレッドかワーカースレッドで行い、**オーディオスレッドからは一切触らない**
- `paint()` 内ではキャッシュ画像を描くだけにする
- デスメタルモードでもボタンの位置・大きさ・操作方法は変えない
- 決まった文言を増やすときは `FixedTexts.h` に追加するだけで済むようにする
- 将来の拡張（今回はやらない）：画数に応じたトゲ密度の調整、左右対称化、ユーザー入力テキストのロゴ化
