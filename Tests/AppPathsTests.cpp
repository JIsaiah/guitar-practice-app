#include "../Source/AppPaths.h"

// ドキュメントの保存先: 前の名前 (GuitarPractice) から新しい名前 (suguhiku) への引き継ぎ
class AppPathsTests final : public juce::UnitTest
{
public:
    AppPathsTests() : juce::UnitTest ("AppPaths", "Library") {}

    void runTest() override
    {
        const auto base = juce::File::createTempFile ("apppaths");

        beginTest ("The old folder is renamed, and the sessions inside are kept");
        {
            base.deleteRecursively();
            const auto session = base.getChildFile ("GuitarPractice/Sessions/song.gppractice");
            session.getParentDirectory().createDirectory();
            session.replaceWithText ("{}");

            expect (AppPaths::documentsFolder (base) == base.getChildFile ("GuitarPractice"), "名前を変える前は前のフォルダを使う");
            expect (AppPaths::migrateDocumentsFolder (base) == AppPaths::MigrationResult::moved);
            expect (! base.getChildFile ("GuitarPractice").exists());
            expect (base.getChildFile ("suguhiku/Sessions/song.gppractice").existsAsFile());
            expect (AppPaths::documentsFolder (base) == base.getChildFile ("suguhiku"));

            // 2回目は何もしない
            expect (AppPaths::migrateDocumentsFolder (base) == AppPaths::MigrationResult::nothingToDo);
        }

        beginTest ("If the new folder already exists, nothing is moved or mixed");
        {
            base.deleteRecursively();
            base.getChildFile ("GuitarPractice/Sessions").createDirectory();
            base.getChildFile ("suguhiku/Sessions").createDirectory();

            expect (AppPaths::migrateDocumentsFolder (base) == AppPaths::MigrationResult::nothingToDo);
            expect (base.getChildFile ("GuitarPractice/Sessions").isDirectory(), "前のフォルダは消さない");
            expect (AppPaths::documentsFolder (base) == base.getChildFile ("suguhiku"));
        }

        beginTest ("First start (no folders): the new name is used");
        {
            base.deleteRecursively();
            base.createDirectory();
            expect (AppPaths::migrateDocumentsFolder (base) == AppPaths::MigrationResult::nothingToDo);
            expect (AppPaths::documentsFolder (base) == base.getChildFile ("suguhiku"));
        }

        base.deleteRecursively();
    }
};

static AppPathsTests appPathsTests;
