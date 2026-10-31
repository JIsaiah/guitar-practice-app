#pragma once

#include <optional>
#include <vector>

// タップの間隔から BPM を求める。2秒以上空いたら測り直す
class TapTempo
{
public:
    std::optional<double> tap (double nowMs)
    {
        if (! taps.empty() && nowMs - taps.back() > 2000.0)
            taps.clear();

        taps.push_back (nowMs);
        if (taps.size() > 8)
            taps.erase (taps.begin());

        if (taps.size() < 2)
            return std::nullopt;

        const auto averageInterval = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        return 60000.0 / averageInterval;
    }

private:
    std::vector<double> taps;
};
