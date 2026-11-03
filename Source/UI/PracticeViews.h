#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

// 未実装の領域を示す枠 (タブ譜、動画など)
class PlaceholderView final : public juce::Component
{
public:
    void setText (const juce::String& newText)
    {
        text = newText;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (findColour (ThemeColourIds::panel));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f)));
        g.drawFittedText (text, getLocalBounds().reduced (12), juce::Justification::centred, 6);
    }

private:
    juce::String text;
};

// タブ譜の枠。タブ譜があるときは、この上に ScoreViewer (WebView) を重ねる。
// タブ譜がないときは、再生位置を小節・拍で大きく表示し、選び方を案内する
class ScoreView final : public juce::Component
{
public:
    void setBarAndBeat (int newBar, int newBeat)
    {
        if (newBar != bar || newBeat != beat)
        {
            bar = newBar;
            beat = newBeat;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour (findColour (ThemeColourIds::panel));
        g.fillRoundedRectangle (bounds.toFloat(), 8.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);

        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (56.0f, juce::Font::bold)));
        g.drawText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat),
                    bounds.withSizeKeepingCentre (bounds.getWidth(), 90), juce::Justification::centred);

        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (14.0f)));
        g.drawText ("タブ譜がありません。上の「タブ譜を選ぶ…」から選べます (Guitar Pro、MusicXML)"_jp,
                    bounds.withSizeKeepingCentre (bounds.getWidth(), 30).translated (0, 70), juce::Justification::centred);
    }

private:
    int bar = 0, beat = 0;
};

// 再生位置のバー。A-B ループの範囲と、経過時間 / 長さも表示する
// - クリックでその位置へ移動する。ドラッグ中は表示だけを動かし、離したときに1回だけ移動する
//   (ドラッグ中に何度も移動させると、YouTube では位置の報告が追いつかず、つまみが前後に飛ぶため)
// - 現在位置は「針」で示す。マウスを乗せるとバーと針が太く長くなり (操作できることを伝える)、針の近くではさらに太くなる
class TimelineView final : public juce::Component
{
public:
    std::function<void (double seconds)> onSeek;

    TimelineView() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setState (double positionSeconds, double lengthSeconds, double loopStart, double loopEnd, bool loopOn)
    {
        position = positionSeconds;
        length = lengthSeconds;
        loopStartSeconds = loopStart;
        loopEndSeconds = loopEnd;
        loopEnabled = loopOn;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto shown = dragging ? dragSeconds : position;

        // 右端に「経過 / 長さ」
        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText (formatTime (shown) + " / " + formatTime (length), getTimeArea(), juce::Justification::centredRight);

        const bool active = hovering || dragging;
        const auto thickness = active ? 8.0f : 4.0f;
        const auto area = getBarArea().toFloat();
        const auto bar = area.withSizeKeepingCentre (area.getWidth(), thickness);

        g.setColour (findColour (ThemeColourIds::border));
        g.fillRoundedRectangle (bar, thickness * 0.5f);

        if (length <= 0.0)
            return;

        if (loopEndSeconds > loopStartSeconds)
        {
            g.setColour (loopEnabled ? findColour (ThemeColourIds::accent).withAlpha (0.35f)
                                     : findColour (ThemeColourIds::textMuted).withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (loopStartSeconds), bar.getY() - 6.0f,
                                                                    xFor (loopEndSeconds), bar.getBottom() + 6.0f));
        }

        g.setColour (findColour (ThemeColourIds::timelineProgress));
        g.fillRoundedRectangle (bar.withRight (xFor (shown)), thickness * 0.5f);

        // 現在位置の針。形は変えずに、太さ・長さ・色だけを変える
        //   ふだん: 細い針 (2px)  /  バーの上: 太い針 (4px, アクセント色)  /  針の近く・ドラッグ中: さらに太く長い針 (6px)
        const bool grabbing = dragging || nearKnob;
        const auto needleWidth = grabbing ? 6.0f : (active ? 4.0f : 2.0f);
        const auto needleHeight = grabbing ? (float) getHeight() - 2.0f : (active ? 28.0f : 20.0f);
        const auto needle = juce::Rectangle<float> (needleWidth, needleHeight).withCentre ({ xFor (shown), area.getCentreY() });

        if (active)
        {
            // 背景と区別できるよう、針の周りを面の色で縁取る
            g.setColour (findColour (ThemeColourIds::panel));
            g.fillRoundedRectangle (needle.expanded (1.5f), needleWidth * 0.5f + 1.5f);
        }

        g.setColour (active ? findColour (ThemeColourIds::accent) : findColour (juce::Label::textColourId));
        g.fillRoundedRectangle (needle, needleWidth * 0.5f);
    }

   #if JUCE_DEBUG
    // 開発用 (DevSnapshots): マウスを乗せた状態を再現する
    void setTestHover (bool hover, bool nearNeedle) { hovering = hover; nearKnob = nearNeedle; repaint(); }
   #endif

    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseExit (const juce::MouseEvent&) override    { hovering = nearKnob = false; repaint(); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hovering = e.x <= getBarArea().getRight();
        nearKnob = hovering && length > 0.0 && std::abs ((float) e.x - xFor (position)) < 16.0f;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (length <= 0.0 || e.x > getBarArea().getRight())
            return;

        dragging = true;
        dragSeconds = secondsAt (e.x);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        dragSeconds = secondsAt (e.x);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        dragging = false;
        position = dragSeconds;   // 次の位置の報告が届くまで、離した位置を表示しておく
        if (onSeek != nullptr)
            onSeek (dragSeconds);
        mouseMove (e);
    }

private:
    static constexpr int timeWidth = 230;

    juce::Rectangle<int> getTimeArea() const { return getLocalBounds().removeFromRight (timeWidth); }
    juce::Rectangle<int> getBarArea() const  { return getLocalBounds().withTrimmedRight (timeWidth + 12).reduced (12, 0); }

    float xFor (double seconds) const
    {
        const auto bar = getBarArea().toFloat();
        return bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, length > 0.0 ? seconds / length : 0.0);
    }

    double secondsAt (int x) const
    {
        const auto bar = getBarArea();
        return length * juce::jlimit (0.0, 1.0, (x - bar.getX()) / (double) juce::jmax (1, bar.getWidth()));
    }

    double position = 0.0, length = 0.0, loopStartSeconds = 0.0, loopEndSeconds = 0.0;
    bool loopEnabled = false;
    bool hovering = false, nearKnob = false, dragging = false;
    double dragSeconds = 0.0;
};

// ずらしの操作: 「◀ -10 ms」[値]「+10 ms ▶」
// - ボタンを押し続けると、連続で変わる (setRepeatSpeed)
// - 値をクリックすると、数値 (ms) を直接入力できる
class OffsetControl final : public juce::Component
{
public:
    std::function<void (int newMs)> onChange;
    std::function<void()> onEditFinished;   // 数値の入力を終えたとき (キー操作を練習画面に戻すため)

    static constexpr int minMs = -5000, maxMs = 5000;

    OffsetControl()
    {
        for (auto* button : { &downButton, &upButton })
        {
            button->setRepeatSpeed (400, 50);   // 0.4 秒押し続けると、0.05 秒ごとにくり返す
            button->setMouseClickGrabsKeyboardFocus (false);
            button->setWantsKeyboardFocus (false);
            addAndMakeVisible (button);
        }
        downButton.onClick = [this] { change (value - step); };
        upButton.onClick = [this] { change (value + step); };

        valueLabel.setEditable (true, false, false);
        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        valueLabel.setTooltip ("クリックすると、数値 (ms) を入力できます"_jp);
        valueLabel.setMouseCursor (juce::MouseCursor::IBeamCursor);
        valueLabel.onEditorShow = [this]
        {
            if (auto* editor = valueLabel.getCurrentTextEditor())
            {
                editor->setInputRestrictions (6, "-+0123456789");
                editor->setText (juce::String (value), false);
                editor->selectAll();
            }
        };
        valueLabel.onTextChange = [this] { change (valueLabel.getText().removeCharacters (" msMS+").getIntValue()); };
        valueLabel.onEditorHide = [this]
        {
            show();
            if (onEditFinished != nullptr)
                onEditFinished();
        };
        addAndMakeVisible (valueLabel);

        setStep (10);
        show();
    }

    void setValue (int ms)
    {
        value = juce::jlimit (minMs, maxMs, ms);
        show();
    }

    void setStep (int ms)
    {
        step = juce::jmax (1, ms);
        downButton.setButtonText (juce::String::fromUTF8 ("◀ -") + juce::String (step) + " ms");
        upButton.setButtonText ("+" + juce::String (step) + juce::String::fromUTF8 (" ms ▶"));
    }

    void setTooltips (const juce::String& down, const juce::String& up)
    {
        downButton.setTooltip (down);
        upButton.setTooltip (up);
    }

    void paint (juce::Graphics& g) override
    {
        // 値の欄に薄い枠を描き、入力できることを示す
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (valueLabel.getBounds().toFloat().reduced (2.0f, 1.5f), 5.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto buttonWidth = juce::jmin (96, r.getWidth() / 3);
        downButton.setBounds (r.removeFromLeft (buttonWidth));
        upButton.setBounds (r.removeFromRight (buttonWidth));
        valueLabel.setBounds (r.reduced (6, 0));
    }

private:
    void change (int ms)
    {
        ms = juce::jlimit (minMs, maxMs, ms);
        if (ms != value)
        {
            value = ms;
            if (onChange != nullptr)
                onChange (value);
        }
        show();
    }

    void show()
    {
        valueLabel.setText ((value > 0 ? "+" : "") + juce::String (value) + " ms", juce::dontSendNotification);
    }

    int value = 0, step = 10;
    juce::TextButton downButton, upButton;
    juce::Label valueLabel;
};

// カード (角の丸い面) を並べる入れ物。右のパネルの、スクロールする部分の中身に使う
class CardColumn final : public juce::Component
{
public:
    std::vector<juce::Rectangle<int>> cards;   // 面を描く場所 (この部品の中の座標)

    void paint (juce::Graphics& g) override
    {
        for (const auto& card : cards)
        {
            g.setColour (findColour (ThemeColourIds::panel));
            g.fillRoundedRectangle (card.toFloat(), 8.0f);
            g.setColour (findColour (ThemeColourIds::border));
            g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 8.0f, 1.0f);
        }
    }
};

// スクロールする枠。見えている範囲が変わったら知らせる (プラグインの画面を出す・隠すの判定に使う)
class ScrollArea final : public juce::Viewport
{
public:
    std::function<void()> onScrolled;

    void visibleAreaChanged (const juce::Rectangle<int>&) override
    {
        if (onScrolled != nullptr)
            onScrolled();
    }
};

// チューナーなどを開いている間、後ろの画面を少し暗くする幕。クリックすると onClick (閉じる)
class DimOverlay final : public juce::Component
{
public:
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.45f)); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick != nullptr) onClick(); }
};

// ホイール1目盛りで、決まった量だけ値を変えるスライダー (設計書 4.3)
// JUCE の標準では、ホイールで動く量が範囲の広さで決まるため、設定画面で決めた量 (wheelStep) にする
class WheelSlider final : public juce::Slider
{
public:
    double wheelStep = 1.0;

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (! isEnabled())
            return;

        // タッチパッドの細かいスクロールは、ある程度たまってから1目盛りとして扱う
        accumulated += wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;
        const auto threshold = wheel.isSmooth ? 0.1f : 0.0001f;

        if (std::abs (accumulated) < threshold)
            return;

        const auto direction = accumulated > 0.0f ? 1.0 : -1.0;
        accumulated = 0.0f;
        setValue (getValue() + direction * wheelStep, juce::sendNotificationSync);
    }

private:
    float accumulated = 0.0f;
};

// ゴミ箱のアイコンのボタン (音源・タブ譜を外す)
class TrashButton final : public juce::Button
{
public:
    TrashButton() : juce::Button ("trash") {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 16.0f);
        auto colour = findColour (ThemeColourIds::textMuted);
        if (highlighted) colour = findColour (ThemeColourIds::accent);
        if (down)        colour = colour.darker (0.2f);
        if (! isEnabled()) colour = colour.withMultipliedAlpha (0.4f);

        juce::Path p;
        p.addRoundedRectangle (r.getX() + 1.5f, r.getY() + 4.0f, r.getWidth() - 3.0f, r.getHeight() - 4.0f, 1.5f);   // 本体
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        g.drawLine (r.getX(), r.getY() + 2.5f, r.getRight(), r.getY() + 2.5f, 1.4f);                                  // ふた
        g.drawLine (r.getCentreX() - 2.5f, r.getY() + 0.5f, r.getCentreX() + 2.5f, r.getY() + 0.5f, 1.4f);            // 取っ手
        for (float dx : { -2.5f, 0.0f, 2.5f })                                                                        // 縦の線
            g.drawLine (r.getCentreX() + dx, r.getY() + 7.0f, r.getCentreX() + dx, r.getBottom() - 3.0f, 1.0f);
    }
};

// 左右の領域の境目のバー。引っ張ると、右のパネルの幅を変える
class SplitterBar final : public juce::Component
{
public:
    std::function<void (int deltaX)> onDragStart, onDrag;   // deltaX: 押した位置からの移動量 (右が正)

    SplitterBar() { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }

    void paint (juce::Graphics& g) override
    {
        const auto hot = isMouseOverOrDragging();
        g.setColour (hot ? findColour (ThemeColourIds::accent) : findColour (ThemeColourIds::border));
        g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (hot ? 3.0f : 2.0f, (float) getHeight()), 1.5f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { if (onDragStart != nullptr) onDragStart (0); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag != nullptr) onDrag (e.getDistanceFromDragStartX()); }
    void mouseUp (const juce::MouseEvent&) override    { repaint(); }
};

// 小さな見出し (ミキサー、ずらし など)。何の部品かが分かるように
class SectionHeading final : public juce::Label
{
public:
    explicit SectionHeading (const juce::String& text = {})
    {
        setText (text, juce::dontSendNotification);
        setFont (juce::FontOptions (12.5f, juce::Font::bold));
        setBorderSize ({ 0, 2, 0, 2 });
    }

    void lookAndFeelChanged() override { setColour (textColourId, findColour (ThemeColourIds::textMuted)); }
    void parentHierarchyChanged() override { lookAndFeelChanged(); }
};
