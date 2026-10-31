#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Text.h"

// 未実装の領域を示す枠 (タブ譜、YouTube プレイヤーなど)
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
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (text, getLocalBounds().reduced (12), juce::Justification::centred, 6);
    }

private:
    juce::String text;
};

// タブ譜の代わりに、再生位置を小節・拍で表示する (alphaTab は PoC-3 で組み込む)
class ScoreView final : public juce::Component
{
public:
    void setScoreName (const juce::String& name) { scoreName = name; }

    void setPosition (double positionSeconds, double lengthSeconds, double bpm, double offsetSeconds)
    {
        position = positionSeconds;
        length = lengthSeconds;
        beatsPerMinute = bpm;
        offset = offsetSeconds;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

        auto header = bounds.removeFromTop (40).reduced (12, 0);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("タブ譜: "_jp + (scoreName.isEmpty() ? "(なし)"_jp : scoreName)
                        + "   ※ alphaTab の表示は PoC-3 で実装"_jp,
                    header, juce::Justification::centredLeft);

        const auto beats = (position - offset) * beatsPerMinute / 60.0;
        const auto bar = beats >= 0.0 ? (int) std::floor (beats / 4.0) + 1 : 0;
        const auto beat = beats >= 0.0 ? (int) std::floor (beats) % 4 + 1 : 0;

        auto centre = bounds.withSizeKeepingCentre (bounds.getWidth(), 160);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (56.0f, juce::Font::bold));
        g.drawText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat),
                    centre.removeFromTop (90), juce::Justification::centred);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (22.0f));
        g.drawText (formatTime (position) + " / " + formatTime (length),
                    centre, juce::Justification::centred);
    }

private:
    juce::String scoreName;
    double position = 0.0, length = 0.0, beatsPerMinute = 120.0, offset = 0.0;
};

// 再生位置のバー。クリック・ドラッグでシークする。A-B ループの範囲も表示する
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
        auto bar = getLocalBounds().toFloat().reduced (0.0f, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (bar, 4.0f);

        if (length <= 0.0)
            return;

        const auto xFor = [&] (double seconds)
        { return bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, seconds / length); };

        if (loopEndSeconds > loopStartSeconds)
        {
            g.setColour ((loopEnabled ? juce::Colours::orange : juce::Colours::grey).withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (loopStartSeconds), bar.getY(),
                                                                    xFor (loopEndSeconds), bar.getBottom()));
        }

        g.setColour (juce::Colours::skyblue.withAlpha (0.6f));
        g.fillRoundedRectangle (bar.withRight (xFor (position)), 4.0f);

        g.setColour (juce::Colours::white);
        g.fillRect (xFor (position) - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override { seekTo (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo (e.x); }

private:
    void seekTo (int x)
    {
        if (length > 0.0 && onSeek != nullptr)
            onSeek (length * juce::jlimit (0.0, 1.0, x / (double) juce::jmax (1, getWidth())));
    }

    double position = 0.0, length = 0.0, loopStartSeconds = 0.0, loopEndSeconds = 0.0;
    bool loopEnabled = false;
};
