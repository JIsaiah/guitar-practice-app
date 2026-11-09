#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"
#include "Icons.h"

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

    // 音源もタブ譜も無いとき: 小節・拍の代わりに、選ぶボタン (練習画面が上に置く) のための案内だけを描く
    void setEmptySession (bool empty)
    {
        if (empty != emptySession)
        {
            emptySession = empty;
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

        if (emptySession)
        {
            g.setColour (findColour (juce::Label::textColourId));
            g.setFont (AppLookAndFeel::scaled (juce::FontOptions (22.0f, juce::Font::bold)));
            g.drawText ("タブ譜か音源を選んでください"_jp, bounds.withSizeKeepingCentre (bounds.getWidth(), 40).translated (0, -60),
                        juce::Justification::centred);
            g.setColour (findColour (ThemeColourIds::textMuted));
            g.setFont (AppLookAndFeel::scaled (juce::FontOptions (14.0f)));
            g.drawText ("どちらか一方だけでも練習できます。あとから両方そろえることもできます"_jp,
                        bounds.withSizeKeepingCentre (bounds.getWidth(), 24).translated (0, -26), juce::Justification::centred);
            return;
        }

        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (56.0f, juce::Font::bold)));
        g.drawText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat),
                    bounds.withSizeKeepingCentre (bounds.getWidth(), 90), juce::Justification::centred);

        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (14.0f)));
        g.drawText ("タブ譜がありません。上の「タブ譜を選択」から選べます (Guitar Pro、MusicXML)"_jp,
                    bounds.withSizeKeepingCentre (bounds.getWidth(), 30).translated (0, 70), juce::Justification::centred);
    }

private:
    int bar = 0, beat = 0;
    bool emptySession = false;
};

// 再生位置のバー。A-B ループの範囲と、経過時間 / 長さも表示する
// - クリックでその位置へ移動する。ドラッグ中は表示だけを動かし、離したときに1回だけ移動する
//   (ドラッグ中に何度も移動させると、YouTube では位置の報告が追いつかず、つまみが前後に飛ぶため)
// - 現在位置は「針」で示す。マウスを乗せるとバーと針が太く長くなり (操作できることを伝える)、針の近くではさらに太くなる
//
// 細かい調整 (拡大モード): バーをダブルクリックして、2回目を押したままにする (0.35 秒) と、
// 針のまわり (前後 2 秒) を拡大する。そのまま左右に動かすと、ミリ秒単位で位置を決められ、離すとそこへ移動する。
// メトロノームの開始位置などを、正確に決めるため (作者の指示)
class TimelineView final : public juce::Component,
                           private juce::Timer
{
public:
    std::function<void (double seconds)> onSeek;

    TimelineView() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    // 目印 (メトロノームの開始位置)。マイナスなら出さない
    void setMarker (double seconds)
    {
        if (! juce::approximatelyEqual (seconds, markerSeconds))
        {
            markerSeconds = seconds;
            repaint();
        }
    }

    bool isZoomed() const noexcept { return zoomed; }

    static constexpr double zoomSpanSeconds = 4.0;   // 拡大したときに見せる範囲 (針の前後 2 秒)
    static constexpr double holdMs = 350.0;          // ダブルクリックの2回目を、これだけ押し続けると拡大

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
        if (zoomed)
        {
            paintZoomed (g);
            return;
        }

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

        if (markerSeconds >= 0.0)
            drawMarker (g, xFor (markerSeconds), bar.getY());

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

        // ダブルクリックの2回目: 押し続けたら拡大する (すぐに離したら何もしない。1回目で、もうそこへ移動している)
        if (e.getNumberOfClicks() >= 2)
        {
            waitingForHold = true;
            holdStartX = e.x;
            startTimer ((int) holdMs);
            return;
        }

        dragging = true;
        dragSeconds = secondsAt (e.x);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (zoomed)
        {
            // ふだん: 針はカーソルの真下 (拡大した範囲の中で、カーソルの位置の時刻)。1px はおよそ 8ms。
            // Shift を押している間: 動かした量の 1/10 だけ動かす (およそ 0.8ms/px)。このときはカーソルと針がずれるので、
            // Shift を離したら、針がまたカーソルの真下に来るように、拡大の範囲をずらす (針は飛ばない)
            const bool fine = e.mods.isShiftDown();

            if (fine)
            {
                if (! fineDrag)
                {
                    fineDrag = true;
                    zoomAnchorSeconds = dragSeconds;
                    holdStartX = e.x;
                }

                const auto perPixel = zoomSpanSeconds / juce::jmax (1, getBarArea().getWidth()) * 0.1;
                dragSeconds = juce::jlimit (0.0, length, zoomAnchorSeconds + (e.x - holdStartX) * perPixel);

                // 細かく動かして範囲の外に出たら、範囲をずらす
                if (dragSeconds < zoomStart || dragSeconds > zoomStart + zoomSpanSeconds)
                    zoomStart = dragSeconds - zoomSpanSeconds * 0.5;
            }
            else
            {
                if (fineDrag)
                {
                    fineDrag = false;
                    alignZoomTo (dragSeconds, e.x);   // 今の針がカーソルの真下に来るように
                }

                dragSeconds = juce::jlimit (0.0, length, zoomStart + zoomFractionAt (e.x) * zoomSpanSeconds);
            }

            repaint();
            return;
        }

        if (! dragging)
            return;

        dragSeconds = secondsAt (e.x);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (waitingForHold)   // 押し続けずに離した
        {
            waitingForHold = false;
            stopTimer();
            return;
        }

        if (! dragging && ! zoomed)
            return;

        dragging = zoomed = false;
        position = dragSeconds;   // 次の位置の報告が届くまで、離した位置を表示しておく
        if (onSeek != nullptr)
            onSeek (dragSeconds);
        mouseMove (e);
    }

   #if JUCE_DEBUG
    // 開発用 (DevSnapshots): 拡大モードの見た目を再現する
    void setTestZoom (double seconds)
    {
        zoomed = true;
        dragSeconds = zoomAnchorSeconds = seconds;
        zoomStart = juce::jlimit (0.0, juce::jmax (0.0, length - zoomSpanSeconds), seconds - zoomSpanSeconds * 0.5);
        repaint();
    }
   #endif

private:
    // ダブルクリックの2回目を押し続けた: 針のまわりを拡大する
    void timerCallback() override
    {
        stopTimer();
        if (! waitingForHold)
            return;

        waitingForHold = false;
        zoomed = true;
        fineDrag = false;

        // 針は、マウスのところ (作者の指示)。拡大しても針がカーソルの真下に来るように、拡大の範囲を決める
        const auto mouseX = getMouseXYRelative().x;
        dragSeconds = secondsAt (mouseX);
        alignZoomTo (dragSeconds, mouseX);
        repaint();
    }

    // 拡大した範囲の中での、x の位置の割合 (0〜1)
    double zoomFractionAt (int x) const
    {
        const auto bar = getBarArea();
        return juce::jlimit (0.0, 1.0, (x - bar.getX()) / (double) juce::jmax (1, bar.getWidth()));
    }

    // seconds の位置が、x の真下に来るように、拡大の範囲をずらす。
    // 曲の頭・終わりの近くでも合わせられるよう、範囲が曲の外にはみ出してもよい (その部分は目盛りを描かない)
    void alignZoomTo (double seconds, int x)
    {
        zoomStart = seconds - zoomFractionAt (x) * zoomSpanSeconds;
    }

    // 拡大モードの絵: 前後 2 秒を、100ms ごとの目盛りで。時刻はミリ秒まで
    void paintZoomed (juce::Graphics& g)
    {
        const auto area = getBarArea().toFloat();
        const auto text = findColour (juce::Label::textColourId);
        const auto accent = findColour (ThemeColourIds::accent);

        g.setColour (findColour (ThemeColourIds::panel));
        g.fillRoundedRectangle (area.expanded (6.0f, 0.0f), 6.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (area.expanded (6.0f, 0.0f).reduced (0.5f), 6.0f, 1.0f);

        const auto xAt = [&] (double seconds)
        {
            return area.getX() + area.getWidth() * (float) ((seconds - zoomStart) / zoomSpanSeconds);
        };

        // 目盛り: 100ms ごと。1秒ごとは長く、時刻を添える
        const auto firstTick = std::ceil (juce::jmax (0.0, zoomStart) * 10.0);
        for (auto tick = firstTick; tick / 10.0 <= juce::jmin (length, zoomStart + zoomSpanSeconds); tick += 1.0)
        {
            const auto seconds = tick / 10.0;
            const auto x = xAt (seconds);
            const bool whole = std::fmod (tick, 10.0) == 0.0;
            const auto h = whole ? area.getHeight() * 0.55f : (std::fmod (tick, 5.0) == 0.0 ? area.getHeight() * 0.35f : area.getHeight() * 0.2f);

            g.setColour (text.withAlpha (whole ? 0.7f : 0.35f));
            g.fillRect (juce::Rectangle<float> (1.0f, h).withCentre ({ x, area.getCentreY() }));

            if (whole)
            {
                g.setFont (AppLookAndFeel::scaled (juce::FontOptions (11.0f)));
                g.drawText (formatTime (seconds), juce::Rectangle<float> (x + 3.0f, area.getY(), 60.0f, 14.0f),
                            juce::Justification::topLeft);
            }
        }

        if (markerSeconds >= zoomStart && markerSeconds <= zoomStart + zoomSpanSeconds)
            drawMarker (g, xAt (markerSeconds), area.getCentreY() - 6.0f);

        // 針
        const auto needle = juce::Rectangle<float> (3.0f, area.getHeight()).withCentre ({ xAt (dragSeconds), area.getCentreY() });
        g.setColour (accent);
        g.fillRoundedRectangle (needle, 1.5f);

        // 右端: ミリ秒までの時刻と、操作の案内
        auto timeArea = getTimeArea();
        g.setColour (text);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (22.0f, juce::Font::bold)));
        g.drawFittedText (formatTimeMs (dragSeconds), timeArea.removeFromTop (getHeight() * 2 / 3), juce::Justification::centredRight, 1);
        g.setColour (text.withAlpha (0.6f));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (11.0f)));
        g.drawFittedText ("左右で調整 (Shift で細かく)・離すと移動"_jp, timeArea, juce::Justification::centredRight, 1);
    }

    // 目印 (小さな下向きの三角): メトロノームの開始位置
    void drawMarker (juce::Graphics& g, float x, float barTop) const
    {
        juce::Path triangle;
        triangle.addTriangle (x - 5.0f, barTop - 9.0f, x + 5.0f, barTop - 9.0f, x, barTop - 1.0f);
        g.setColour (findColour (ThemeColourIds::good));
        g.fillPath (triangle);
    }

    static juce::String formatTimeMs (double seconds)
    {
        const auto totalMs = juce::roundToInt (juce::jmax (0.0, seconds) * 1000.0);
        return juce::String (totalMs / 60000).paddedLeft ('0', 2) + ":" + juce::String ((totalMs / 1000) % 60).paddedLeft ('0', 2)
               + "." + juce::String (totalMs % 1000).paddedLeft ('0', 3);
    }

    // 時間の表示の幅は、文字の幅に合わせる (バーを時間の表示のすぐ手前まで伸ばす。作者の指示)。
    // 数字が変わるたびにバーの長さが変わらないよう、決まった見本の文字列で測る
    int getTimeWidth() const
    {
        const auto font = juce::Font (AppLookAndFeel::scaled (juce::FontOptions (24.0f, juce::Font::bold)));
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, "00:00.0 / 00:00.0")) + 4;
    }

    juce::Rectangle<int> getTimeArea() const { return getLocalBounds().removeFromRight (getTimeWidth()); }
    juce::Rectangle<int> getBarArea() const  { return getLocalBounds().withTrimmedRight (getTimeWidth() + 14).withTrimmedLeft (12); }

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
    double markerSeconds = -1.0;

    // 拡大モード
    bool waitingForHold = false, zoomed = false, fineDrag = false;
    int holdStartX = 0;
    double zoomStart = 0.0, zoomAnchorSeconds = 0.0;
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
        auto colour = findColour (ThemeColourIds::textMuted);
        if (highlighted) colour = findColour (ThemeColourIds::accent);
        if (down)        colour = colour.darker (0.2f);
        if (! isEnabled()) colour = colour.withMultipliedAlpha (0.4f);

        // 作者が置いたアイコン (`svg icons/trash`)
        Icons::draw (g, Icons::trash, getLocalBounds().toFloat().withSizeKeepingCentre (24.0f, 24.0f), colour);
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

// メトロノームのビジュアライザー。小節の拍子の分子の数だけドットを並べ、今の拍を点ける
// (4/4 ならドット4個、9/8 なら9個)。小節の頭 (1つ目) は少し大きく描く
class MetronomeDots final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    // numerator/denominator: 今の小節の拍子。activeBeat: 点ける拍 (0 から)。-1 なら点けない (止まっているとき)
    void setState (int numerator, int denominator, int activeBeat)
    {
        numerator = juce::jlimit (1, 32, numerator);
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (numerator != count || denominator != unit || activeBeat != active)
        {
            if (activeBeat != active && activeBeat >= 0)
                litAtMs = now;   // 新しい拍: ここから光を弱めていく

            count = numerator;
            unit = denominator;
            active = activeBeat;
            repaint();
        }
        else if (active >= 0 && now - litAtMs < glowMs + 50.0)
        {
            repaint();   // 光が弱まっていく途中 (呼ぶ側のタイマー 30 回/秒 で描き直す)
        }
    }

    int getCount() const noexcept { return count; }

    // 大きく表示する (画面の真ん中に出すとき)。ドットと拍子の文字を、部品の高さに合わせて大きくする
    void setLarge (bool shouldBeLarge) { large = shouldBeLarge; repaint(); }

    std::function<void()> onClick;   // クリックされた (練習画面が、大きな表示を出す・消す)

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        // 左に拍子 ("4/4")
        const auto labelSize = large ? area.getHeight() * 0.32f : 13.0f;
        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (labelSize, juce::Font::bold)));
        g.drawText (juce::String (count) + "/" + juce::String (unit), area.removeFromLeft (labelSize * 2.6f),
                    juce::Justification::centredLeft);

        // ドットの大きさは、数が多いときは幅に収まるよう小さくする
        const auto pitch = juce::jmin (area.getHeight(), area.getWidth() / (float) count);
        const auto radius = juce::jlimit (2.0f, large ? area.getHeight() * 0.3f : 7.0f, pitch * 0.36f);
        const auto accent = findColour (ThemeColourIds::accent);
        const auto idle = findColour (ThemeColourIds::textMuted);

        for (int i = 0; i < count; ++i)
        {
            const auto centre = juce::Point<float> (area.getX() + pitch * ((float) i + 0.5f), area.getCentreY());
            const auto r = i == 0 ? radius * 1.25f : radius;
            const auto dot = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

            if (i == active)
            {
                // 光: 拍の頭で強く、だんだん弱まる。外側ほど薄い円を重ねて、ぼんやり光って見せる
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - litAtMs;
                const auto glow = (float) juce::jlimit (0.0, 1.0, 1.0 - elapsed / glowMs);
                const auto glowColour = i == 0 ? accent.brighter (0.3f) : accent;

                for (int ring = 4; ring >= 1; --ring)
                {
                    const auto spread = r * (1.0f + 0.22f * (float) ring);
                    g.setColour (glowColour.withAlpha ((0.10f + 0.22f * glow) / (float) ring));
                    g.fillEllipse (juce::Rectangle<float> (spread * 2.0f, spread * 2.0f).withCentre (centre));
                }

                g.setColour ((i == 0 ? accent : accent.withMultipliedAlpha (0.85f)).brighter (0.5f * glow));
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (idle.withAlpha (0.55f));
                g.drawEllipse (dot.reduced (0.75f), 1.5f);
            }
        }
    }

private:
    static constexpr double glowMs = 350.0;   // 拍の頭の光が弱まるまでの時間
    int count = 4, unit = 4, active = -1;
    double litAtMs = 0.0;
    bool large = false;
};

// メトロノームのビジュアライザーを、画面の真ん中に大きく出すウィンドウ (小さいドットをクリックすると出る)。
// タブ譜や動画は WebView (Windows の別の窓) で、アプリの部品より手前に描かれるため、
// 練習画面の部品として重ねると隠れてしまう。そこで、枠のない別のウィンドウにして、アプリのウィンドウの手前に出す。
// クリックすると閉じる
class MetronomeBigWindow final : public juce::Component
{
public:
    MetronomeBigWindow()
    {
        setOpaque (false);   // 角を丸くして、まわりを透かす
        dots.setLarge (true);
        dots.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (dots);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClose;
    MetronomeDots& getDots() noexcept { return dots; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (findColour (ThemeColourIds::panel).withAlpha (0.94f));
        g.fillRoundedRectangle (bounds, 18.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (bounds, 18.0f, 1.5f);

        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
        g.drawText ("クリックで閉じる"_jp, getLocalBounds().removeFromBottom (30), juce::Justification::centred);
    }

    void resized() override
    {
        dots.setBounds (getLocalBounds().reduced (36, 30).withTrimmedBottom (10));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onClose != nullptr)
            onClose();
    }

private:
    MetronomeDots dots;
};

// 右クリックにも反応するボタン (「ここから開始」: 左クリックで決める、右クリックで解除のメニュー)
class RightClickableButton final : public juce::TextButton
{
public:
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick != nullptr)
                onRightClick();
            return;
        }
        TextButton::mouseDown (e);
    }
};
