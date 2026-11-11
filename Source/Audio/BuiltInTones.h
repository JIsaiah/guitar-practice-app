#pragma once

#include <juce_core/juce_core.h>
#include <BinaryData.h>

// アプリに同梱した音色のファイル (作者の指示: 「歪み」は、作者が選んだ NAM のモデルと IR で鳴らす)
//
// ファイルは実行ファイルに埋め込んである (CMakeLists.txt の juce_add_binary_data。元は 音色プリセット/ のファイル)。
// NAM の読み込みはファイルの場所を受け取るので、初めて使うときに
//   %APPDATA%\GuitarPractice\built-in-tones\
// に書き出し、その場所を返す。中身が同じ (大きさが同じ) なら書き出し直さない
namespace BuiltInTones
{
    struct Files
    {
        juce::File amp, cabinet;   // 書き出せなかったものは juce::File()
    };

    inline juce::File writeOut (const char* originalName, const juce::File& folder)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const auto* name = BinaryData::namedResourceList[i];
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) != originalName)
                continue;

            int size = 0;
            const auto* data = BinaryData::getNamedResource (name, size);
            if (data == nullptr || size <= 0)
                return {};

            const auto file = folder.getChildFile (originalName);
            if (file.existsAsFile() && file.getSize() == (juce::int64) size)
                return file;

            if (folder.createDirectory() && file.replaceWithData (data, (size_t) size))
                return file;

            return {};
        }
        return {};
    }

    inline Files getDriveTone()
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("GuitarPractice").getChildFile ("built-in-tones");
        return { writeOut ("drive-amp.nam", folder), writeOut ("drive-cab.wav", folder) };
    }
}
