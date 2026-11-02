#pragma once

#include "../Library/Setlist.h"

// 開き方によって、ループ・速度の読み書き先を切り替える (setlist-mode.md 3.6)
//
//   | 設定                         | Standalone     | Setlist                   |
//   | 速度・A-B ループ              | セッション      | エントリの overrides       |
//   | 音色・ミキサー・ずらし・その他  | セッション      | セッション                 |
//
// 練習画面は、これまでどおり Session の値 (speedPercent、loop…) を読み書きする。
// - 開くとき: makeWorkingCopy() で、速度・ループをエントリの専用設定に置き換えた「画面で使う値」を作る
// - 保存するとき: split() で、セッションに保存する分 (速度・ループは単体の値に戻す) と、エントリに保存する分に分ける
class PlaybackSettingsResolver
{
public:
    PlaybackSettingsResolver() = default;                                          // Standalone
    explicit PlaybackSettingsResolver (const SetlistEntry::Overrides& overrides);  // Setlist

    bool usesOverrides() const noexcept { return overrides.has_value(); }

    Session makeWorkingCopy (const Session& stored);

    struct Split
    {
        Session session;                                   // セッションファイルに保存する
        std::optional<SetlistEntry::Overrides> overrides;  // Setlist のとき、エントリに保存する
    };
    Split split (const Session& working) const;

    // 速度の範囲 (練習画面のスライダーと同じ)
    static constexpr int minSpeedPercent = 50, maxSpeedPercent = 200;

private:
    std::optional<SetlistEntry::Overrides> overrides;

    // Setlist のとき: 単体セッションの速度・ループ (保存するときに、このまま戻す)
    int storedSpeedPercent = 100;
    int storedLoopStartMs = 0, storedLoopEndMs = 0;
    bool storedLoopEnabled = false;
};
