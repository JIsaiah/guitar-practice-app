#pragma once

#include <array>
#include "../Text.h"

// 音色の種類
//   clean / crunch: 簡易な音色 (AudioEngine で tanh をかける。ファイルなしで、すぐ音が出る)
//   drive: 「歪み」。アプリに同梱した NAM のモデルと IR で鳴らす (Audio/BuiltInTones.h。「NAM」のボタンとは別)
//   nam: NAM のアンプのモデル (.nam) とキャビネットの IR (.wav)。ファイルを選んで使う (設計書 #6)
enum class ToneType
{
    clean = 0,
    crunch,
    drive,
    nam,
    plugin   // VST3 プラグイン (TONE3000 など)
};

struct TonePreset
{
    const char* id;
    const char* name;  // UTF-8
    ToneType type;
};

// 簡易な音色 (ファイルなしで使える2つ)。「クランチ」は作者の指示で外した
// (前に「クランチ」を選んで保存したセッションは、見つからない ID として「クリーン」で開く。findTonePresetIndex)
inline const std::array<TonePreset, 2>& getTonePresets()
{
    static const std::array<TonePreset, 2> presets {{
        { "clean-01",  "クリーン", ToneType::clean },
        { "drive-01",  "歪み",     ToneType::drive },
    }};
    return presets;
}

// NAM の音色 (セッションの tone.preset_id に入る値)
inline constexpr const char* namTonePresetId = "nam";

// VST3 プラグイン (TONE3000) の音色 (セッションの tone.preset_id に入る値)
inline constexpr const char* pluginTonePresetId = "tone3000";

inline int findTonePresetIndex (const juce::String& id)
{
    const auto& presets = getTonePresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (id == presets[i].id)
            return (int) i;

    return 0;
}
