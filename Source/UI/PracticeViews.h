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

// 再生位置のバー。クリック・ドラッグでシークする。A-B ループの範囲と、経過時間 / 長さも表示する
class TimelineView final : public juce::Component
{
public:
    std::function<void (double seconds)> onSeek;

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
        // 右端に「経過 / 長さ」。数字は等幅にして、値が変わっても揺れないようにする
        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText (formatTime (position) + " / " + formatTime (length), getTimeArea(), juce::Justification::centredRight);

        auto bar = getBarArea().toFloat().reduced (0.0f, 10.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.fillRoundedRectangle (bar, 4.0f);

        if (length <= 0.0)
            return;

        const auto xFor = [&] (double seconds)
        { return bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, seconds / length); };

        if (loopEndSeconds > loopStartSeconds)
        {
            g.setColour (loopEnabled ? findColour (ThemeColourIds::accent).withAlpha (0.35f)
                                     : findColour (ThemeColourIds::textMuted).withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (loopStartSeconds), bar.getY() - 4.0f,
                                                                    xFor (loopEndSeconds), bar.getBottom() + 4.0f));
        }

        g.setColour (findColour (ThemeColourIds::timelineProgress));
        g.fillRoundedRectangle (bar.withRight (xFor (position)), 4.0f);

        g.setColour (findColour (juce::Label::textColourId));
        g.fillRect (xFor (position) - 1.0f, 2.0f, 2.0f, (float) getHeight() - 4.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { seekTo (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo (e.x); }

private:
    static constexpr int timeWidth = 230;

    juce::Rectangle<int> getTimeArea() const { return getLocalBounds().removeFromRight (timeWidth); }
    juce::Rectangle<int> getBarArea() const  { return getLocalBounds().withTrimmedRight (timeWidth + 12); }

    void seekTo (int x)
    {
        const auto bar = getBarArea();
        if (length > 0.0 && onSeek != nullptr && x <= bar.getRight())
            onSeek (length * juce::jlimit (0.0, 1.0, (x - bar.getX()) / (double) juce::jmax (1, bar.getWidth())));
    }

    double position = 0.0, length = 0.0, loopStartSeconds = 0.0, loopEndSeconds = 0.0;
    bool loopEnabled = false;
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
