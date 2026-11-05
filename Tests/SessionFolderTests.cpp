#include "../Source/Library/SessionFolders.h"

// 一覧のフォルダ分け
class SessionFolderTests final : public juce::UnitTest
{
public:
    SessionFolderTests() : juce::UnitTest ("SessionFolders", "Library") {}

    void runTest() override
    {
        using namespace SessionFolders;

        std::vector<SessionStore::Entry> entries (4);
        entries[0].title = "A"; entries[0].folder = "ライブ"_jp;
        entries[1].title = "B"; entries[1].folder = "ライブ"_jp;
        entries[2].title = "C"; entries[2].folder = "練習中"_jp;
        entries[3].title = "D";

        beginTest ("フォルダの一覧 (保存した空のフォルダも出す)"_jp);
        const auto names = collectNames (entries, { "練習中"_jp, "あとで"_jp });
        expectEquals (names.size(), 3);
        expect (names.contains ("あとで"_jp));

        const auto items = buildItems (entries, names);
        expectEquals ((int) items.size(), 5);   // すべて + 3 + フォルダなし
        expect (items.front().kind == Item::Kind::all);
        expectEquals (items.front().count, 4);
        expect (items.back().kind == Item::Kind::none);
        expectEquals (items.back().count, 1);

        for (const auto& item : items)
            if (item.name == "ライブ"_jp)
                expectEquals (item.count, 2);
            else if (item.name == "あとで"_jp)
                expectEquals (item.count, 0);

        beginTest ("フォルダがなければ「すべて」だけ"_jp);
        std::vector<SessionStore::Entry> plain (2);
        expectEquals ((int) buildItems (plain, collectNames (plain, {})).size(), 1);

        beginTest ("絞り込み"_jp);
        expectEquals ((int) filter (entries, allKey).size(), 4);
        expectEquals ((int) filter (entries, noneKey).size(), 1);
        expectEquals ((int) filter (entries, keyFor ("ライブ"_jp)).size(), 2);
        expectEquals ((int) filter (entries, keyFor ("あとで"_jp)).size(), 0);

        beginTest ("フォルダ名の確認"_jp);
        expect (checkName ("", names).isNotEmpty());
        expect (checkName ("ライブ"_jp, names).isNotEmpty());
        expect (checkName ("新しい"_jp, names).isEmpty());
    }
};

static SessionFolderTests sessionFolderTests;
