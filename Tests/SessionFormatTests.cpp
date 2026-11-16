#include "../Source/Library/SessionFormat.h"

// セッション一覧の「形式」の列
class SessionFormatTests final : public juce::UnitTest
{
public:
    SessionFormatTests() : juce::UnitTest ("SessionFormat", "Library") {}

    void runTest() override
    {
        beginTest ("A tab file wins over the audio, and shows its extension");
        {
            expectEquals (SessionFormat::describe ("C:/Tabs/song.gp", "youtube"), juce::String (".gp"));
            expectEquals (SessionFormat::describe ("C:/Tabs/Song.PDF", "local"), juce::String (".pdf"));
            expectEquals (SessionFormat::describe ("C:/Tabs/old.gp5", "none"), juce::String (".gp5"));
        }

        beginTest ("Without a tab file, the audio decides");
        {
            expectEquals (SessionFormat::describe ({}, "youtube"), juce::String ("URL"));
            expectEquals (SessionFormat::describe ({}, "local"), "ローカルファイル"_jp);
            expectEquals (SessionFormat::describe ({}, "none"), "—"_jp);
        }
    }
};

static SessionFormatTests sessionFormatTests;
