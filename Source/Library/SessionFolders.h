#pragma once

#include <vector>
#include "../Model/SessionStore.h"
#include "../Text.h"

// 一覧のフォルダ分け (画面に依存しない計算。単体テストあり: Tests/SessionFolderTests.cpp)
//   - フォルダは1段だけ。セッションのファイルの "folder" に名前を書く (空ならフォルダなし)
//   - 中身のないフォルダも残すため、名前の一覧はアプリの設定にも保存する (AppSettings::getSessionFolders)
//   - 選んでいるフォルダは「キー」で表す: "all" (すべて)、"none" (フォルダなし)、"folder:名前"
namespace SessionFolders
{
    inline const juce::String allKey = "all", noneKey = "none";

    inline juce::String keyFor (const juce::String& folderName) { return "folder:" + folderName; }

    struct Item
    {
        enum class Kind { all, named, none };
        Kind kind = Kind::all;
        juce::String name;   // named のときだけ
        int count = 0;       // 入っているセッションの数

        juce::String getKey() const
        {
            return kind == Kind::all ? allKey : kind == Kind::none ? noneKey : keyFor (name);
        }

        juce::String getLabel() const
        {
            return kind == Kind::all ? "すべて"_jp : kind == Kind::none ? "フォルダなし"_jp : name;
        }
    };

    // 設定に保存した名前と、セッションが使っている名前を合わせた、フォルダの名前 (重複なし、名前順)
    inline juce::StringArray collectNames (const std::vector<SessionStore::Entry>& entries, const juce::StringArray& saved)
    {
        juce::StringArray names (saved);
        for (const auto& e : entries)
            if (e.folder.isNotEmpty())
                names.addIfNotAlreadyThere (e.folder);

        names.removeEmptyStrings();
        names.removeDuplicates (false);
        names.sortNatural();
        return names;
    }

    // 左の列に並べるもの: すべて → 各フォルダ → フォルダなし (フォルダが1つもなければ「すべて」だけ)
    inline std::vector<Item> buildItems (const std::vector<SessionStore::Entry>& entries, const juce::StringArray& names)
    {
        std::vector<Item> items;
        items.push_back ({ Item::Kind::all, {}, (int) entries.size() });

        for (const auto& name : names)
        {
            Item item { Item::Kind::named, name, 0 };
            for (const auto& e : entries)
                if (e.folder == name)
                    ++item.count;
            items.push_back (item);
        }

        if (! names.isEmpty())
        {
            Item none { Item::Kind::none, {}, 0 };
            for (const auto& e : entries)
                if (e.folder.isEmpty() || ! names.contains (e.folder))
                    ++none.count;
            items.push_back (none);
        }

        return items;
    }

    inline bool matches (const juce::String& key, const SessionStore::Entry& entry)
    {
        if (key == noneKey)
            return entry.folder.isEmpty();
        if (key.startsWith ("folder:"))
            return entry.folder == key.fromFirstOccurrenceOf ("folder:", false, false);
        return true;   // すべて (知らないキーも、すべてとして扱う)
    }

    inline std::vector<SessionStore::Entry> filter (const std::vector<SessionStore::Entry>& entries, const juce::String& key)
    {
        std::vector<SessionStore::Entry> result;
        for (const auto& e : entries)
            if (matches (key, e))
                result.push_back (e);
        return result;
    }

    // 新しいフォルダ名・名前の変更の確認。問題があれば、その説明 (なければ空)
    inline juce::String checkName (const juce::String& name, const juce::StringArray& existing)
    {
        if (name.trim().isEmpty())
            return "名前を入力してください"_jp;
        if (name.containsAnyOf ("\r\n"))
            return "名前に改行は使えません"_jp;
        if (existing.contains (name.trim()))
            return "同じ名前のフォルダがあります"_jp;
        return {};
    }
}
