#include "TunerPanel.h"
#include "../Audio/PitchDetector.h"
#include "../Theme/DeathMetalLookAndFeel.h"
#include "../Text.h"

namespace
{
    constexpr double rangeCents = 50.0;   // ゲージの端 = ±50 セント
    constexpr double inTuneCents = 5.0;   // これ以内なら「合っている」
    constexpr int segmentsPerSide = 10;   // 片側の目盛りの数 (1つ 5 セント)

    // 標準チューニングの弦 (MIDI 番号 → 弦の番号)
    int guitarStringFor (int midi)
    {
        switch (midi)
        {
            case 40: return 6;  // E2
            case 45: return 5;  // A2
            case 50: return 4;  // D3
            case 55: return 3;  // G3
            case 59: return 2;  // B3
            case 64: return 1;  // E4
            default: return 0;
        }
    }

    juce::FontOptions font (float size, int style = juce::Font::plain)
    {
        return AppLookAndFeel::scaled (juce::FontOptions (size, style));
    }
}

TunerPanel::TunerPanel (AudioEngine& e)
    : engine (e)
{
    closeButton.setButtonText ("閉じる"_jp);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    // 前回の残りを捨ててから測り始める
    while (engine.readTunerInput (incoming.data(), analysisSize) > 0) {}

    engine.setTunerActive (true);
    startTimerHz (30);   // 表示をなめらかにするため。音程の解析は 1/2 の頻度
}

TunerPanel::~TunerPanel()
{
    engine.setTunerActive (false);
}

void TunerPanel::analyse()
{
    bool received = false;

    for (;;)
    {
        const auto count = engine.readTunerInput (incoming.data(), analysisSize);
        if (count <= 0)
            break;

        // 古いものを前に詰め、新しいものを後ろに足す
        std::move (history.begin() + count, history.end(), history.begin());
        std::copy_n (incoming.data(), count, history.end() - count);
        received = true;
    }

    if (! received)
        return;

    const auto detected = detectPitchHz (history.data(), analysisSize, engine.getDeviceSampleRate());

    if (detected > 0.0)
    {
        frequency = detected;
        silentFrames = 0;

        const auto midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);
        targetCents = (midi - std::round (midi)) * 100.0;
    }
    else if (++silentFrames > 15)  // 1秒ほど無音なら表示を消す
    {
        frequency = 0.0;
        targetCents = 0.0;
    }
}

void TunerPanel::timerCallback()
{
    if (--analyseCountdown <= 0)
    {
        analyseCountdown = 2;
        analyse();
    }

    shownCents += (targetCents - shownCents) * 0.4;
    repaint();
}

#if JUCE_DEBUG
void TunerPanel::setTestFrequency (double hz)
{
    stopTimer();
    frequency = hz;
    const auto midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);
    targetCents = shownCents = (midi - std::round (midi)) * 100.0;
}
#endif

void TunerPanel::paint (juce::Graphics& g)
{
    const auto panel = findColour (ThemeColourIds::panel);
    const auto border = findColour (ThemeColourIds::border);
    const auto text = findColour (juce::Label::textColourId);
    const auto muted = findColour (ThemeColourIds::textMuted);
    const auto accent = findColour (ThemeColourIds::accent);
    const auto good = findColour (ThemeColourIds::good);

    const bool hasNote = frequency > 0.0;
    const bool inTune = hasNote && std::abs (targetCents) < inTuneCents;
    const bool flat = hasNote && targetCents <= -inTuneCents;
    const bool sharp = hasNote && targetCents >= inTuneCents;

    // ---- カード ----
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, 12.0f, 1.0f);

    auto area = getLocalBounds().reduced (24, 18);
    auto header = area.removeFromTop (22);
    g.setColour (muted);
    g.setFont (font (14.0f));
    g.drawText ("チューナー"_jp, header, juce::Justification::centredLeft);
    area.removeFromBottom (40);   // 閉じるボタン

    // ---- 音名と、低い／高いの矢印 ----
    auto noteRow = area.removeFromTop (92);
    auto flatArea = noteRow.removeFromLeft (110);
    auto sharpArea = noteRow.removeFromRight (110);

    const auto drawArrow = [&] (juce::Rectangle<int> r, bool pointsRight, bool on, const juce::String& label)
    {
        auto tri = r.withSizeKeepingCentre (26, 30).toFloat().translated (0.0f, -8.0f);
        juce::Path p;
        if (pointsRight) p.addTriangle (tri.getTopLeft(), tri.getBottomLeft(), { tri.getRight(), tri.getCentreY() });
        else             p.addTriangle (tri.getTopRight(), tri.getBottomRight(), { tri.getX(), tri.getCentreY() });
        g.setColour (on ? accent : border);
        g.fillPath (p);
        g.setColour (on ? text : muted);
        g.setFont (font (12.0f));
        g.drawText (label, r.withTop (juce::roundToInt (tri.getBottom()) + 4).withHeight (18), juce::Justification::centredTop);
    };
    // 低いときは右向き (上げる)、高いときは左向き (下げる) の矢印が点く
    drawArrow (flatArea, true, flat, "低い (締める)"_jp);
    drawArrow (sharpArea, false, sharp, "高い (緩める)"_jp);

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (hasNote)
    {
        const auto midi = (int) std::round (69.0 + 12.0 * std::log2 (frequency / 440.0));
        g.setColour (inTune ? good : text);
        g.setFont (font (64.0f, juce::Font::bold));
        g.drawText (noteNames[((midi % 12) + 12) % 12], noteRow, juce::Justification::centred);

        const auto stringNumber = guitarStringFor (midi);
        if (stringNumber > 0)
        {
            g.setColour (muted);
            g.setFont (font (13.0f));
            g.drawText (juce::String (stringNumber) + "弦"_jp, noteRow.removeFromRight (40).removeFromTop (28), juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (muted);
        g.setFont (font (16.0f));
        g.drawText ("弦を1本鳴らしてください"_jp, noteRow, juce::Justification::centred);
    }

    // ---- 左右のゲージ ----
    area.removeFromTop (10);
    auto gauge = area.removeFromTop (44);
    const int count = segmentsPerSide * 2 + 1;
    const auto gap = 4.0f;
    const auto segmentWidth = ((float) gauge.getWidth() - gap * (float) (count - 1)) / (float) count;
    const auto position = juce::jlimit (-rangeCents, rangeCents, shownCents);
    const auto litIndex = juce::roundToInt (position / (rangeCents / segmentsPerSide));   // -10〜+10

    for (int i = -segmentsPerSide; i <= segmentsPerSide; ++i)
    {
        const auto x = (float) gauge.getX() + (float) (i + segmentsPerSide) * (segmentWidth + gap);
        const bool centre = i == 0;
        const auto height = centre ? (float) gauge.getHeight() : (float) gauge.getHeight() * (0.55f + 0.03f * (float) (segmentsPerSide - std::abs (i)));
        auto segment = juce::Rectangle<float> (x, (float) gauge.getCentreY() - height * 0.5f, segmentWidth, height);

        // 中央から今の位置までを点ける (どちらにどれだけずれているかが、長さで分かる)
        const bool lit = hasNote && ((litIndex > 0 && i > 0 && i <= litIndex) || (litIndex < 0 && i < 0 && i >= litIndex) || (centre && inTune));
        juce::Colour colour = border;
        if (lit)
            colour = centre ? good : accent.withAlpha (i == litIndex ? 1.0f : 0.55f);

        g.setColour (colour);
        g.fillRoundedRectangle (segment, 2.5f);

        if (centre)
        {
            g.setColour (inTune ? good : muted);
            g.drawRoundedRectangle (segment.expanded (2.0f), 3.0f, 1.5f);   // 中央は枠でも示す
        }
    }

    // ---- 数値 ----
    area.removeFromTop (8);
    auto info = area.removeFromTop (22);
    g.setFont (font (13.0f));
    g.setColour (muted);
    g.drawText ("-50", info, juce::Justification::centredLeft);
    g.drawText ("+50", info, juce::Justification::centredRight);

    if (hasNote)
    {
        g.setColour (inTune ? good : text);
        g.setFont (font (15.0f, inTune ? juce::Font::bold : juce::Font::plain));
        const auto cents = juce::roundToInt (targetCents);
        g.drawText ((inTune ? "OK   "_jp : juce::String()) + (cents > 0 ? "+" : "") + juce::String (cents) + " cent   "
                        + juce::String (frequency, 1) + " Hz",
                    info, juce::Justification::centred);
    }
}

void TunerPanel::resized()
{
    closeButton.setBounds (getLocalBounds().reduced (18).removeFromBottom (32).removeFromRight (110));
}
