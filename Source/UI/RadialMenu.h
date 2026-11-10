#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ラジアルメニュー (円形のメニュー。作者の指示・開発者用メモ)
//
// 開き方: マウスの左と右のボタンを同時に押し、0.2 秒押し続けると、カーソルを中心に出る。
//         真ん中のボタン (ホイールの押し込み) を 0.2 秒押し続けても出る (トラックボール向け)。
//         押している間だけ出ていて、ボタンを離すと、カーソルのある扇形の操作を実行して消える。
//         中心の近くで離したら、何もしない。
// 表示:   背景を少し暗くし、円を出す。円が画面 (練習画面) からはみ出るときは、
//         はみ出ない範囲で、できるだけカーソルの近くに中心をずらす (カーソルはどれかの扇形の上になる)。
//         選んでいる扇形は、外に伸び、色が濃くなり、文字が大きくなる (なめらかに動く)。
//
// しくみ:
//   - タブ譜と YouTube は WebView (Windows の別の窓) で、その上のマウスの操作はアプリの部品に届かない。
//     そこで、タイマー (60 回/秒) でマウスのボタンと位置を直接調べる (Platform/MouseState)
//   - メニューは、WebView より手前に出すため、枠のない別のウィンドウに描く (クリックは下に通す)
//   - 押し始めたところにアプリのボタンなどがあると、離したときにそれも押されてしまう。
//     メニューを出した時点で、その部品を一時的に使えなくする (離したあとに戻す)
class RadialMenu final : private juce::Timer
{
public:
    struct Item
    {
        juce::String label;
        std::function<void()> action;
        bool enabled = true;
    };

    // target: メニューを出す画面 (練習画面)。makeItems: 開くたびに、並べる操作を作る (上から時計回り)
    RadialMenu (juce::Component& target, std::function<std::vector<Item>()> makeItems);
    ~RadialMenu() override;

    bool isShowing() const noexcept { return window != nullptr; }

    static constexpr double holdMs = 200.0;   // 左右を同時に押してから、出るまで (作者の指示で 0.5 → 0.2 秒)

    // 出した・消した (練習画面が、その間タブ譜を止める: マウスでハイライト・スクロールしないように)
    std::function<void (bool open)> onOpenChanged;

    class Window;   // 描く部品 (DevSnapshots で、見た目を確かめるのにも使う)

private:
    void timerCallback() override;
    bool canStartAt (juce::Point<int> screenPos) const;
    void show (juce::Point<int> screenPos);
    void close (bool runAction);

    juce::Component& target;
    std::function<std::vector<Item>()> makeItems;

    enum class Phase { idle, holding, open, waitRelease };
    Phase phase = Phase::idle;
    double holdStartMs = 0.0;

    std::unique_ptr<Window> window;
    juce::Component::SafePointer<juce::Component> suppressed;   // 一時的に使えなくした部品
};

// メニューの絵 (背景を暗くする板 + 円)。部品の大きさ = 練習画面の大きさ
class RadialMenu::Window final : public juce::Component
{
public:
    explicit Window (std::vector<Item> items);

    // centre: 円の中心 (この部品の中の位置)。部品の大きさに合わせて、はみ出ないようにずらす
    void placeAround (juce::Point<int> mousePos);

    // カーソルの位置 (この部品の中) から、選んでいる操作を決める。-1 = なし (中心の近く)
    void updateHover (juce::Point<float> mousePos);
    int getHovered() const noexcept { return hovered; }
    const std::vector<Item>& getItems() const noexcept { return items; }

    // アニメーションを1コマ進める (dtSeconds 秒)。まだ動いていれば true
    bool advance (double dtSeconds);

   #if JUCE_DEBUG
    // 開発用 (DevSnapshots): アニメーションを終えた状態で描く
    void finishAnimationForTest();
   #endif

    void paint (juce::Graphics& g) override;

    float getRadius() const noexcept { return radius; }

private:
    juce::Path makeSector (int index, float outerRadius) const;

    std::vector<Item> items;
    juce::Point<float> centre;
    float radius = 260.0f;
    int hovered = -1;
    float openAmount = 0.0f;              // 0 → 1 (開くとき)
    std::vector<float> hoverAmounts;      // 扇形ごとの、選ばれ具合 (0 → 1。なめらかに変わる)
};
