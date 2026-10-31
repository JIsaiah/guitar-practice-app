#include "TunerPanel.h"
#include "../Audio/PitchDetector.h"
#include "../Text.h"

TunerPanel::TunerPanel (AudioEngine& e)
    : engine (e)
{
    closeButton.setButtonText ("閉じる"_jp);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    // 前回の残りを捨ててから測り始める
    while (engine.readTunerInput (incoming.data(), analysisSize) > 0) {}

    engine.setTunerActive (true);
    startTimerHz (12);
}

TunerPanel::~TunerPanel()
{
    engine.setTunerActive (false);
}

void TunerPanel::timerCallback()
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
    }
    else if (++silentFrames > 12)  // 1秒ほど無音なら表示を消す
    {
        frequency = 0.0;
    }

    repaint();
}

void TunerPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1e2228));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto area = getLocalBounds().reduced (20);
    area.removeFromBottom (40);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("チューナー"_jp, area.removeFromTop (24), juce::Justification::centredLeft);

    if (frequency <= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (20.0f));
        g.drawText ("弦を1本鳴らしてください"_jp, area, juce::Justification::centred);
        return;
    }

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);
    const auto nearest = (int) std::round (midi);
    const auto cents = (midi - nearest) * 100.0;
    const auto inTune = std::abs (cents) < 5.0;

    g.setColour (inTune ? juce::Colours::lightgreen : juce::Colours::white);
    g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
    g.drawText (juce::String (noteNames[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1),
                area.removeFromTop (80), juce::Justification::centred);

    // -50〜+50 セントのメーター
    auto meter = area.removeFromTop (24).reduced (20, 6).toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (meter.getCentreX() - 1.0f, meter.getY() - 4.0f, 2.0f, meter.getHeight() + 8.0f);

    const auto needleX = meter.getCentreX() + (float) (juce::jlimit (-50.0, 50.0, cents) / 50.0) * meter.getWidth() * 0.5f;
    g.setColour (inTune ? juce::Colours::lightgreen : juce::Colours::orange);
    g.fillRect (needleX - 3.0f, meter.getY() - 6.0f, 6.0f, meter.getHeight() + 12.0f);

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (juce::String (frequency, 1) + " Hz   " + (cents >= 0 ? "+" : "") + juce::String (cents, 0) + " cent",
                area.removeFromTop (30), juce::Justification::centred);
}

void TunerPanel::resized()
{
    closeButton.setBounds (getLocalBounds().reduced (20).removeFromBottom (32).withSizeKeepingCentre (120, 32));
}
