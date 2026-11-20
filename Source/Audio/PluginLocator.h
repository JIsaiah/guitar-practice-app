#pragma once

#include <juce_core/juce_core.h>

// VST3 プラグインの置き場所から、名前でプラグインを探す
// Windows の VST3 の標準の置き場所: C:\Program Files\Common Files\VST3 (と、ユーザーごとの置き場所)
namespace PluginLocator
{
    // TONE3000 プラグインのダウンロードページ (公式。無料。インストーラーで入れる)
    inline constexpr const char* tone3000DownloadUrl = "https://www.tone3000.com/plugin";

    inline juce::Array<juce::File> getVst3Folders()
    {
        return {
            juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3"),
            juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3"),
        };
    }

    // 名前に nameContains を含む .vst3 を探す (大文字・小文字は区別しない)。見つからなければ空の File
    inline juce::File findVst3 (const juce::String& nameContains)
    {
        juce::Array<juce::File> found;

        for (const auto& folder : getVst3Folders())
            if (folder.isDirectory())
                for (const auto& file : folder.findChildFiles (juce::File::findFilesAndDirectories, true, "*.vst3"))
                    if (file.getFileNameWithoutExtension().containsIgnoreCase (nameContains))
                        found.add (file);

        if (found.isEmpty())
            return {};

        // .vst3 は「フォルダ (中に同じ名前の .vst3 がある)」のことが多い。いちばん外側 (パスが短いもの) を使う
        std::sort (found.begin(), found.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().length() < b.getFullPathName().length(); });
        return found.getFirst();
    }

    inline juce::File findTone3000()
    {
        return findVst3 ("TONE3000");
    }

    // 同時に2つ作ると落ちるプラグイン (作者の PC で確かめた: Archetype Misha Mansoor X は、2つ目ができた直後に落ちる。
    // Archetype Gojira・NeuralAmpModeler は3つでも落ちない。Misha と別のプラグインの組み合わせも落ちない)。
    // 同じ作りの「Archetype ... X」も、念のため同じ扱いにする。これらは1つだけ作り、プリセットの間で使い回す
    inline bool isSingleInstance (const juce::File& vst3)
    {
        const auto name = vst3.getFileNameWithoutExtension();
        return name.startsWithIgnoreCase ("Archetype ") && name.endsWithIgnoreCase (" X");
    }
}
