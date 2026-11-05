#pragma once

#include <juce_core/juce_core.h>

// 動作ログ。%APPDATA%\GuitarPractice\app.log に「時刻 内容」を1行ずつ追記する。
// アプリが急に消えたとき、「自分で終わった (終了の知らせがある)」のか「落ちた (途中で切れている)」のかを
// 見分けるため。大きくなりすぎたら (1MB) 消して書き直す。
// メッセージスレッドから呼ぶ (オーディオスレッドからは呼ばない: ファイルの書き込みはしてはいけない)
namespace AppLog
{
    inline juce::File getFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GuitarPractice").getChildFile ("app.log");
    }

    inline void write (const juce::String& text)
    {
        auto file = getFile();
        file.getParentDirectory().createDirectory();

        if (file.getSize() > 1024 * 1024)
            file.deleteFile();

        file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S ") + text + "\n", false, false, "\r\n");
        DBG ("[app.log] " << text);
    }
}
