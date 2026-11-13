#include "../Source/Web/ScoreFileData.h"

// タブ譜のページに渡すデータ: 音声入りの .gp から音声 (Content/Assets/) を抜く
class ScoreFileDataTests final : public juce::UnitTest
{
public:
    ScoreFileDataTests() : juce::UnitTest ("ScoreFileData", "Score") {}

    void runTest() override
    {
        const auto folder = juce::File::createTempFile ("scorefiledata");
        folder.createDirectory();

        beginTest ("A .gp with embedded audio: the audio is removed, the score stays the same");
        {
            const auto gp = folder.getChildFile ("with-audio.gp");
            writeZip (gp, { { "VERSION", "7.0" },
                            { "Content/score.gpif", "<GPIF>score</GPIF>" },
                            { "Content/Assets/track.wav", juce::String::repeatedString ("x", 200000) },
                            { "Content/BinaryStylesheet", "style" } });

            const auto stripped = ScoreFileData::withoutEmbeddedAssets (gp);
            expect (stripped.has_value());
            if (stripped.has_value())
            {
                expect (stripped->getSize() < 10000, "音声を抜いたので小さい");

                juce::MemoryInputStream input (*stripped, false);
                juce::ZipFile zip (input);
                expectEquals (zip.getNumEntries(), 3);
                expect (zip.getIndexOfFileName ("Content/Assets/track.wav") < 0);
                expectEquals (readEntry (zip, "Content/score.gpif"), juce::String ("<GPIF>score</GPIF>"));
                expectEquals (readEntry (zip, "VERSION"), juce::String ("7.0"));
                expectEquals (readEntry (zip, "Content/BinaryStylesheet"), juce::String ("style"));
            }
        }

        beginTest ("A .gp without embedded audio is passed as it is");
        {
            const auto gp = folder.getChildFile ("no-audio.gp");
            writeZip (gp, { { "Content/score.gpif", "<GPIF/>" } });
            expect (! ScoreFileData::withoutEmbeddedAssets (gp).has_value());

            juce::MemoryBlock data;
            expect (ScoreFileData::load (gp, data));
            expectEquals ((juce::int64) data.getSize(), gp.getSize());
        }

        beginTest ("A file that is not a zip (.gp5 / MusicXML) is passed as it is");
        {
            const auto gp5 = folder.getChildFile ("old.gp5");
            gp5.replaceWithText ("FICHIER GUITAR PRO v5.00 ...");
            expect (! ScoreFileData::withoutEmbeddedAssets (gp5).has_value());

            juce::MemoryBlock data;
            expect (ScoreFileData::load (gp5, data));
            expectEquals (data.toString(), juce::String ("FICHIER GUITAR PRO v5.00 ..."));
        }

        folder.deleteRecursively();

        // 開発用: 環境変数 SUGUHIKU_STRIP_GP_IN に .gp を指定すると、音声を抜いたものを SUGUHIKU_STRIP_GP_OUT に書く
        // (alphaTab が読めるかを、ブラウザで確かめるため。ふだんのテストでは何もしない)
        const auto in = juce::SystemStats::getEnvironmentVariable ("SUGUHIKU_STRIP_GP_IN", {});
        const auto out = juce::SystemStats::getEnvironmentVariable ("SUGUHIKU_STRIP_GP_OUT", {});
        if (in.isNotEmpty() && out.isNotEmpty())
        {
            beginTest ("Write a stripped copy of SUGUHIKU_STRIP_GP_IN");
            juce::MemoryBlock data;
            expect (ScoreFileData::load (juce::File (in), data));
            expect (juce::File (out).replaceWithData (data.getData(), data.getSize()));
        }
    }

private:
    static void writeZip (const juce::File& file, std::initializer_list<std::pair<const char*, juce::String>> entries)
    {
        juce::ZipFile::Builder builder;
        for (const auto& [name, text] : entries)
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true),
                              6, name, juce::Time::getCurrentTime());

        file.deleteFile();
        juce::FileOutputStream out (file);
        builder.writeToStream (out, nullptr);
    }

    static juce::String readEntry (juce::ZipFile& zip, const juce::String& name)
    {
        const auto index = zip.getIndexOfFileName (name);
        if (index < 0)
            return {};
        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
        return stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
    }
};

static ScoreFileDataTests scoreFileDataTests;
