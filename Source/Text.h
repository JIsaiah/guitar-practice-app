#pragma once

#include <juce_core/juce_core.h>

// 日本語の文字列リテラルを juce::String にする ("一覧"_jp)
// juce::String (const char*) は ASCII 前提のため、UTF-8 として明示的に変換する
inline juce::String operator""_jp (const char* text, std::size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}

// 秒を "mm:ss.d" 形式にする
inline juce::String formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const auto minutes = (int) (seconds / 60.0);
    const auto rest = seconds - minutes * 60.0;
    return juce::String (minutes).paddedLeft ('0', 2) + ":" + juce::String (rest, 1).paddedLeft ('0', 4);
}
