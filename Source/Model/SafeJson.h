#pragma once

#include <juce_core/juce_core.h>

// 保存したファイル (セッション・セットリスト・記録) の JSON を、安全に読む
//
// JUCE の JSON::parse は、[ や { が入れ子になるたびに自分を呼び直す (再帰)。入れ子の深さに上限が無いので、
// 「[[[[[…」と何十万も重ねた小さなファイルで、スタック (関数の呼び出しを覚える場所) があふれ、アプリが落ちる。
// セッションの一覧は起動のたびに全部のファイルを読むので、そういうファイルを Sessions に入れられると、
// 消すまで起動できなくなる (人から受け取ったセッションで起きうる。セキュリティ.md)。
// ここでは、読む前に大きさと入れ子の深さを確かめ、超えていれば「読めなかった」(空の var) とする
namespace SafeJson
{
    // アプリが書くファイルは、プラグインの設定を入れても数 MB。それより十分大きい上限
    inline constexpr juce::int64 maxFileBytes = 64LL * 1024 * 1024;

    // アプリが書く JSON の入れ子は 10 段もない
    inline constexpr int maxDepth = 128;

    // 入れ子の深さが maxNesting 以内か (文字列の中の [ { は数えない)
    inline bool isNestingWithin (const juce::String& text, int maxNesting = maxDepth)
    {
        int depth = 0;
        bool inString = false, escaped = false;

        for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
        {
            const auto c = *p;

            if (inString)
            {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    inString = false;
                continue;
            }

            if (c == '"')
                inString = true;
            else if (c == '[' || c == '{')
            {
                if (++depth > maxNesting)
                    return false;
            }
            else if (c == ']' || c == '}')
                --depth;
        }
        return true;
    }

    // 入れ子が深すぎるものは読まない (空の var)
    inline juce::var parse (const juce::String& text)
    {
        if (! isNestingWithin (text))
            return {};
        return juce::JSON::parse (text);
    }

    // ファイルを読む。無い・大きすぎる・入れ子が深すぎる・JSON でないときは空の var
    inline juce::var parseFile (const juce::File& file)
    {
        if (! file.existsAsFile() || file.getSize() > maxFileBytes)
            return {};
        return parse (file.loadFileAsString());
    }
}
