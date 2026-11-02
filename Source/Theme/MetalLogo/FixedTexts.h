#pragma once

#include "../../Text.h"

// ロゴにする「決まった文言」の一覧 (death-metal-mode.md 4.6)
// ここに無い文字列はロゴにしない。増やすときは、ここに足すだけでよい
// 記号 (◀ ▶ ■ ⚙ +) はロゴに入れない。通常モードの表示文字列は、それぞれの画面が持つ
enum class FixedTextId
{
    // 一覧画面
    AppTitle, Sessions, Setlists, NewSession, Open, Delete, Settings, DeathMetalMode,

    // セッション画面
    BackToList, Save, Details, Play, Stop, Tap, Metronome, Loop, Tuner
};

inline juce::String getFixedText (FixedTextId id)
{
    switch (id)
    {
        case FixedTextId::AppTitle:       return "Guitar Practice";
        case FixedTextId::Sessions:       return "セッション一覧"_jp;
        case FixedTextId::Setlists:       return "セットリスト"_jp;
        case FixedTextId::NewSession:     return "新しいセッション"_jp;
        case FixedTextId::Open:           return "開く"_jp;
        case FixedTextId::Delete:         return "削除"_jp;
        case FixedTextId::Settings:       return "設定"_jp;
        case FixedTextId::DeathMetalMode: return "デスメタルモード"_jp;
        case FixedTextId::BackToList:     return "一覧"_jp;
        case FixedTextId::Save:           return "保存"_jp;
        case FixedTextId::Details:        return "詳細"_jp;
        case FixedTextId::Play:           return "再生"_jp;
        case FixedTextId::Stop:           return "停止"_jp;
        case FixedTextId::Tap:            return "TAP";
        case FixedTextId::Metronome:      return "メトロノーム"_jp;
        case FixedTextId::Loop:           return "ループ"_jp;
        case FixedTextId::Tuner:          return "チューナー"_jp;
    }
    return {};
}

// FixedTextId の全部 (調整ツールでまとめて試すときに使う)
inline constexpr FixedTextId allFixedTextIds[] {
    FixedTextId::AppTitle, FixedTextId::Sessions, FixedTextId::Setlists, FixedTextId::NewSession, FixedTextId::Open,
    FixedTextId::Delete, FixedTextId::Settings, FixedTextId::DeathMetalMode,
    FixedTextId::BackToList, FixedTextId::Save, FixedTextId::Details, FixedTextId::Play,
    FixedTextId::Stop, FixedTextId::Tap, FixedTextId::Metronome, FixedTextId::Loop, FixedTextId::Tuner
};
