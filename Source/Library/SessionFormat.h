#pragma once

#include <juce_core/juce_core.h>
#include "../Text.h"

// セッション一覧の「形式」の列に出す文字 (作者の指示: アーティストと BPM の間)
//   タブ譜があれば、そのファイルの種類 (.gp、.pdf、.gp5、.xml など)
//   タブ譜が無ければ、音源の種類 (YouTube なら「URL」、音楽ファイルなら「ローカルファイル」)
//   どちらも無ければ「—」
namespace SessionFormat
{
    // scorePath: タブ譜のファイル。sourceType: セッションのファイルの source.type ("local" / "youtube" / "none")
    inline juce::String describe (const juce::String& scorePath, const juce::String& sourceType)
    {
        if (scorePath.isNotEmpty())
        {
            const auto extension = juce::File::createFileWithoutCheckingPath (scorePath).getFileExtension().toLowerCase();
            if (extension.isNotEmpty())
                return extension;
        }

        if (sourceType == "youtube") return "URL";
        if (sourceType == "local")   return "ローカルファイル"_jp;
        return "—"_jp;
    }
}
