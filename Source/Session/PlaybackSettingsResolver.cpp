#include "PlaybackSettingsResolver.h"

PlaybackSettingsResolver::PlaybackSettingsResolver (const SetlistEntry::Overrides& o)
    : overrides (o)
{
}

Session PlaybackSettingsResolver::makeWorkingCopy (const Session& stored)
{
    auto working = stored;
    if (! overrides.has_value())
        return working;

    storedSpeedPercent = stored.speedPercent;
    storedLoopStartMs = stored.loopStartMs;
    storedLoopEndMs = stored.loopEndMs;
    storedLoopEnabled = stored.loopEnabled;

    working.speedPercent = juce::jlimit (minSpeedPercent, maxSpeedPercent, juce::roundToInt (overrides->speed * 100.0));
    working.loopStartMs = juce::roundToInt (overrides->loopStartSec * 1000.0);
    working.loopEndMs = juce::roundToInt (overrides->loopEndSec * 1000.0);
    working.loopEnabled = overrides->loopEnabled;
    return working;
}

PlaybackSettingsResolver::Split PlaybackSettingsResolver::split (const Session& working) const
{
    Split result { working, std::nullopt };
    if (! overrides.has_value())
        return result;

    auto o = *overrides;
    o.speed = working.speedPercent / 100.0;
    o.loopStartSec = working.loopStartMs / 1000.0;
    o.loopEndSec = working.loopEndMs / 1000.0;
    o.loopEnabled = working.loopEnabled;
    result.overrides = o;

    result.session.speedPercent = storedSpeedPercent;
    result.session.loopStartMs = storedLoopStartMs;
    result.session.loopEndMs = storedLoopEndMs;
    result.session.loopEnabled = storedLoopEnabled;
    return result;
}
