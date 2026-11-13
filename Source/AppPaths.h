#pragma once

#include <juce_core/juce_core.h>

// ドキュメントの中の保存先 (セッション・セットリスト)
//   ドキュメント\suguhiku\Sessions、ドキュメント\suguhiku\Setlists
// 前の名前は「GuitarPractice」(作者の指示で、アプリの名前に合わせて変えた)。
// 起動したときに、前の名前のフォルダしか無ければ、フォルダの名前を変えて引き継ぐ (migrateDocumentsFolder)。
// 名前を変えられなかったとき (ほかのアプリがファイルを開いているなど) は、前のフォルダをそのまま使い、次の起動でもう一度試す
// (設定・動作の記録は %APPDATA%\GuitarPractice のまま。こちらは使う人が開く場所ではないので変えない)
namespace AppPaths
{
    inline constexpr const char* folderName = "suguhiku";
    inline constexpr const char* oldFolderName = "GuitarPractice";

    inline juce::File documents()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    // 保存先のフォルダ。新しい名前のフォルダがあればそれ、無くて前の名前のフォルダがあれば前のもの
    inline juce::File documentsFolder (const juce::File& base = documents())
    {
        const auto current = base.getChildFile (folderName);
        const auto old = base.getChildFile (oldFolderName);
        return ! current.isDirectory() && old.isDirectory() ? old : current;
    }

    enum class MigrationResult { nothingToDo, moved, failed };

    // 前の名前のフォルダを、新しい名前に変える。新しい名前のフォルダがすでにあれば何もしない (中身を混ぜない)
    inline MigrationResult migrateDocumentsFolder (const juce::File& base = documents())
    {
        const auto current = base.getChildFile (folderName);
        const auto old = base.getChildFile (oldFolderName);

        if (current.exists() || ! old.isDirectory())
            return MigrationResult::nothingToDo;

        return old.moveFileTo (current) ? MigrationResult::moved : MigrationResult::failed;
    }
}
