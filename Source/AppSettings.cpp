#include "AppSettings.h"

namespace
{
    constexpr const char* skipKey = "skipSeconds";
    constexpr const char* wheelSpeedKey = "wheelStepSpeed";
    constexpr const char* wheelBpmKey = "wheelStepBpm";
    constexpr const char* wheelVolumeKey = "wheelStepVolume";
    constexpr const char* wheelOffsetKey = "wheelStepOffset";
    constexpr const char* sidePanelWidthKey = "sidePanelWidth";
}

AppSettings::AppSettings (juce::PropertiesFile& f)
    : file (f)
{
    skipSeconds = juce::jlimit (minSkipSeconds, maxSkipSeconds, file.getIntValue (skipKey, 5));
    wheel.speedPercent = juce::jlimit (1, 50, file.getIntValue (wheelSpeedKey, 5));
    wheel.bpm = juce::jlimit (1, 20, file.getIntValue (wheelBpmKey, 1));
    wheel.volumePercent = juce::jlimit (1, 50, file.getIntValue (wheelVolumeKey, 5));
    wheel.offsetMs = juce::jlimit (1, 500, file.getIntValue (wheelOffsetKey, 10));
    sidePanelWidth = juce::jlimit (minSidePanelWidth, maxSidePanelWidth, file.getIntValue (sidePanelWidthKey, 360));
}

void AppSettings::setSkipSeconds (int seconds)
{
    seconds = juce::jlimit (minSkipSeconds, maxSkipSeconds, seconds);
    if (seconds != skipSeconds)
    {
        skipSeconds = seconds;
        save();
    }
}

void AppSettings::setWheelSteps (const WheelSteps& steps)
{
    wheel = steps;
    save();
}

void AppSettings::setSidePanelWidth (int width)
{
    // 引っ張っている間に何度も呼ばれるので、知らせは出さずに保存だけする
    sidePanelWidth = juce::jlimit (minSidePanelWidth, maxSidePanelWidth, width);
    file.setValue (sidePanelWidthKey, sidePanelWidth);
}

void AppSettings::save()
{
    file.setValue (skipKey, skipSeconds);
    file.setValue (wheelSpeedKey, wheel.speedPercent);
    file.setValue (wheelBpmKey, wheel.bpm);
    file.setValue (wheelVolumeKey, wheel.volumePercent);
    file.setValue (wheelOffsetKey, wheel.offsetMs);
    sendChangeMessage();
}
