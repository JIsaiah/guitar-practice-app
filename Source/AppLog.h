#pragma once

#include <juce_core/juce_core.h>
#include "Text.h"   // 日本語は "…"_jp で渡す (ただの "…" だと文字化けする)

// 動作ログ。%APPDATA%\GuitarPractice\app.log に「時刻 内容」を1行ずつ追記する。
// アプリが急に消えたとき、「自分で終わった (終了の知らせがある)」のか「落ちた (途中で切れている)」のかを
// 見分けるため。大きくなりすぎたら (1MB) 消して書き直す。
// メッセージスレッドから呼ぶ (オーディオスレッドからは呼ばない: ファイルの書き込みはしてはいけない)。
// 落ちたときの記録 (Platform/CrashLog) だけは、落ちたスレッドから呼ぶ
namespace AppLog
{
    // 開発用のテスト (--crash-test) で、書き先を一時フォルダに変える
    inline juce::File& fileOverride() { static juce::File file; return file; }

    inline juce::File getFile()
    {
        if (fileOverride() != juce::File())
            return fileOverride();

        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GuitarPractice").getChildFile ("app.log");
    }

    // 開発用の起動 (--render-logos など) では書かない (作者のログを、テストの記録で埋めないため)
    inline bool& enabledFlag() { static bool enabled = true; return enabled; }
    inline void setEnabled (bool shouldWrite) { enabledFlag() = shouldWrite; }

    inline const juce::String& endMarker() { static const auto text = "終了"_jp; return text; }

    inline void write (const juce::String& text)
    {
        if (! enabledFlag())
            return;

        auto file = getFile();
        file.getParentDirectory().createDirectory();

        if (file.getSize() > 1024 * 1024)
            file.deleteFile();

        file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S ") + text + "\n", false, false, "\r\n");
        DBG ("[app.log] " << text);
    }

    // 「YYYY-MM-DD HH:MM:SS 」で始まる行か (呼び出しの流れなど、複数行の記録の2行目以降ではない)
    inline bool isEntryLine (const juce::String& line)
    {
        return line.length() > 20 && line[4] == '-' && line[7] == '-' && line[10] == ' ' && line[13] == ':';
    }

    // 最後の記録 (時刻を除いた内容)。呼び出しの流れは飛ばして、その前の「落ちました」などを返す。ファイルがなければ空
    inline juce::String lastEntry()
    {
        juce::StringArray lines;
        getFile().readLines (lines);

        for (int i = lines.size() - 1; i >= 0; --i)
            if (isEntryLine (lines[i]) && ! lines[i].substring (20).startsWith ("呼び出しの流れ"_jp))
                return lines[i].substring (20);   // "YYYY-MM-DD HH:MM:SS " の後

        return {};
    }

    // 起動したときに呼ぶ: 前回が「終了」で終わっていなければ、落ちた (か、強制終了された) ことを記録する
    inline void noteStart (const juce::String& version)
    {
        const auto last = lastEntry();
        if (last.isNotEmpty() && last != endMarker())
            write ("!! 前回は正しく終了していません (落ちたか、強制終了された)。最後の記録: "_jp + last);

        write ("起動 ("_jp + version + ")");
    }
}
