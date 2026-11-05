#include "AppSettings.h"

namespace
{
    constexpr const char* skipKey = "skipSeconds";
    constexpr const char* preStartDelayKey = "preStartDelaySeconds";
    constexpr const char* songGapKey = "setlistCountdownSeconds";   // 前の "songGapSeconds" (既定 5) は引き継がない
    constexpr const char* expandedSetlistsKey = "expandedSetlists";
    constexpr const char* wheelSpeedKey = "wheelStepSpeed";
    constexpr const char* wheelBpmKey = "wheelStepBpm";
    constexpr const char* wheelVolumeKey = "wheelStepVolume";
    constexpr const char* wheelOffsetKey = "wheelStepOffset";
    constexpr const char* sidePanelWidthKey = "sidePanelWidth";
    constexpr const char* scoreEqualBarsKey = "scoreEqualBars";
    constexpr const char* libraryTabKey = "libraryTab";
    constexpr const char* guitarMasterKey = "guitarMasterPercent";
}

AppSettings::AppSettings (juce::PropertiesFile& f)
    : file (f)
{
    skipSeconds = juce::jlimit (minSkipSeconds, maxSkipSeconds, file.getIntValue (skipKey, 5));
    preStartDelaySeconds = juce::jlimit (0, maxPreStartDelaySeconds, file.getIntValue (preStartDelayKey, defaultPreStartDelaySeconds));
    songGapSeconds = juce::jlimit (0, maxSongGapSeconds, file.getIntValue (songGapKey, defaultSongGapSeconds));
    guitarMasterPercent = juce::jlimit (0, maxGuitarMasterPercent, file.getIntValue (guitarMasterKey, 100));
    wheel.speedPercent = juce::jlimit (1, 50, file.getIntValue (wheelSpeedKey, 5));
    wheel.bpm = juce::jlimit (1, 20, file.getIntValue (wheelBpmKey, 1));
    wheel.volumePercent = juce::jlimit (1, 50, file.getIntValue (wheelVolumeKey, 5));
    wheel.offsetMs = juce::jlimit (1, 500, file.getIntValue (wheelOffsetKey, 10));
    sidePanelWidth = juce::jlimit (minSidePanelWidth, maxSidePanelWidth, file.getIntValue (sidePanelWidthKey, 400));
    scoreEqualBars = file.getBoolValue (scoreEqualBarsKey, false);
    libraryTab = juce::jlimit (0, 1, file.getIntValue (libraryTabKey, 0));
}

void AppSettings::setLibraryTab (int tab)
{
    // 一覧のタブを押したときだけ変わるので、知らせは出さずに保存する
    libraryTab = juce::jlimit (0, 1, tab);
    file.setValue (libraryTabKey, libraryTab);
}

void AppSettings::setScoreEqualBars (bool equal)
{
    // 練習画面のトグルで変えるだけなので、知らせは出さずに保存する
    scoreEqualBars = equal;
    file.setValue (scoreEqualBarsKey, equal);
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

void AppSettings::setPreStartDelaySeconds (int seconds)
{
    seconds = juce::jlimit (0, maxPreStartDelaySeconds, seconds);
    if (seconds != preStartDelaySeconds)
    {
        preStartDelaySeconds = seconds;
        save();
    }
}

void AppSettings::setSongGapSeconds (int seconds)
{
    seconds = juce::jlimit (0, maxSongGapSeconds, seconds);
    if (seconds != songGapSeconds)
    {
        songGapSeconds = seconds;
        save();
    }
}

void AppSettings::setGuitarMasterPercent (int percent)
{
    percent = juce::jlimit (0, maxGuitarMasterPercent, percent);
    if (percent != guitarMasterPercent)
    {
        guitarMasterPercent = percent;
        save();
    }
}

juce::StringArray AppSettings::getExpandedSetlists() const
{
    return juce::StringArray::fromTokens (file.getValue (expandedSetlistsKey), ",", {});
}

void AppSettings::setExpandedSetlists (const juce::StringArray& setlistIds)
{
    // カードを開閉したときだけ変わるので、知らせは出さずに保存する
    file.setValue (expandedSetlistsKey, setlistIds.joinIntoString (","));
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
    file.setValue (preStartDelayKey, preStartDelaySeconds);
    file.setValue (songGapKey, songGapSeconds);
    file.setValue (guitarMasterKey, guitarMasterPercent);
    file.setValue (wheelSpeedKey, wheel.speedPercent);
    file.setValue (wheelBpmKey, wheel.bpm);
    file.setValue (wheelVolumeKey, wheel.volumePercent);
    file.setValue (wheelOffsetKey, wheel.offsetMs);
    sendChangeMessage();
}
