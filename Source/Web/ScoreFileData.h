#pragma once

#include <juce_core/juce_core.h>
#include <optional>

// タブ譜のファイルを、タブ譜のページ (alphaTab) に渡す形にする
//
// Guitar Pro 7 以降のファイル (.gp) の中身は zip で、曲の音声 (Guitar Pro の「オーディオトラック」) を
// Content/Assets/ に .wav などで丸ごと入れられる。音声入りのファイルは 50〜100 MB になり、
// alphaTab がそれを毎回すべて展開するため、開くのに 5 秒以上かかっていた (93 MB のファイルで 5.7 秒)。
// alphaTab の表示と音 (MIDI) には音声を使わないので、音声を抜いた小さい zip にして渡す (同じファイルで 0.6 秒)。
// 元のファイルは書き換えない (メモリの中で作る)
namespace ScoreFileData
{
    // zip の中の、曲の音声などの素材 (Guitar Pro が使う場所)
    inline bool isEmbeddedAsset (const juce::String& entryName)
    {
        return entryName.startsWith ("Content/Assets/");
    }

    // 音声などの素材を抜いた zip を作る。zip でない (.gp5・.gpx・MusicXML など)、抜くものが無い、作れなかったときは nullopt
    inline std::optional<juce::MemoryBlock> withoutEmbeddedAssets (const juce::File& file)
    {
        juce::ZipFile zip (file);
        const auto count = zip.getNumEntries();
        if (count == 0)
            return std::nullopt;

        bool hasAsset = false;
        for (int i = 0; i < count && ! hasAsset; ++i)
            hasAsset = isEmbeddedAsset (zip.getEntry (i)->filename);
        if (! hasAsset)
            return std::nullopt;

        juce::ZipFile::Builder builder;
        for (int i = 0; i < count; ++i)
        {
            const auto* entry = zip.getEntry (i);
            if (isEmbeddedAsset (entry->filename) || entry->filename.endsWith ("/"))   // 素材とフォルダの項目は入れない
                continue;

            // 中身は writeToStream のときに読まれる。stream は zip を使うので、zip は builder より長く生きる (先に宣言している)
            std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (i));
            if (stream == nullptr)
                return std::nullopt;
            builder.addEntry (std::move (stream), 6, entry->filename, entry->fileTime);
        }

        juce::MemoryOutputStream out;
        if (! builder.writeToStream (out, nullptr))
            return std::nullopt;
        return out.getMemoryBlock();
    }

    // ページに渡すデータ。音声入りの .gp は音声を抜き、それ以外はファイルをそのまま読む
    inline bool load (const juce::File& file, juce::MemoryBlock& data)
    {
        if (auto stripped = withoutEmbeddedAssets (file))
        {
            data = std::move (*stripped);
            return true;
        }
        return file.loadFileAsData (data);
    }
}
