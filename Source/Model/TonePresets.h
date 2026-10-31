#pragma once

#include <array>
#include "../Text.h"

// 音色スロットの種類。NAM を組み込むまでの仮実装 (AudioEngine で簡易な歪みをかける)
enum class ToneType
{
    clean = 0,
    crunch,
    drive
};

struct TonePreset
{
    const char* id;
    const char* name;  // UTF-8
    ToneType type;
};

inline const std::array<TonePreset, 3>& getTonePresets()
{
    static const std::array<TonePreset, 3> presets {{
        { "clean-01",  "クリーン", ToneType::clean },
        { "crunch-01", "クランチ", ToneType::crunch },
        { "drive-01",  "歪み",     ToneType::drive },
    }};
    return presets;
}

inline int findTonePresetIndex (const juce::String& id)
{
    const auto& presets = getTonePresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (id == presets[i].id)
            return (int) i;

    return 0;
}
