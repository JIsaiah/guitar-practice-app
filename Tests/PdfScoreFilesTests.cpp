#include <juce_core/juce_core.h>
#include "../Source/Model/Session.h"

// PDF のタブ譜は2つまで (Session::setScoreFiles): 名前の順で1つ目がリード、2つ目がバッキング
class PdfScoreFilesTests final : public juce::UnitTest
{
public:
    PdfScoreFilesTests() : juce::UnitTest ("PdfScoreFiles", "Model") {}

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tabs");
        const auto a = dir.getChildFile ("song-02.pdf"), b = dir.getChildFile ("song-10.pdf"), gp = dir.getChildFile ("tab.gp");

        beginTest ("Two PDFs: name order decides lead and backing");
        {
            Session s;
            s.backingActive = true;
            expect (s.setScoreFiles ({ b, a }));   // 選んだ順が逆でも、名前の順 (02 → 10)
            expectEquals (s.scorePath, a.getFullPathName());
            expectEquals (s.pdfBackingPath, b.getFullPathName());
            expect (! s.backingActive);
            expectEquals (s.currentScorePath(), a.getFullPathName());
            s.backingActive = true;
            expectEquals (s.currentScorePath(), b.getFullPathName());
        }

        beginTest ("One PDF, or a PDF with a .gp, uses only the first file");
        {
            Session s;
            s.pdfBackingPath = "old.pdf";
            expect (! s.setScoreFiles ({ a }));
            expectEquals (s.scorePath, a.getFullPathName());
            expect (s.pdfBackingPath.isEmpty());

            expect (! s.setScoreFiles ({ gp, a }));
            expectEquals (s.scorePath, a.getFullPathName());   // 名前の順で song-02.pdf が先 (tab.gp より前)
            expect (s.pdfBackingPath.isEmpty());
        }

        beginTest ("A .gp score never shows the PDF backing");
        {
            Session s;
            s.scorePath = gp.getFullPathName();
            s.pdfBackingPath = b.getFullPathName();
            s.backingActive = true;
            expect (! s.hasPdfBacking());
            expectEquals (s.currentScorePath(), gp.getFullPathName());
        }

        beginTest ("The backing PDF is saved and read back");
        {
            Session s;
            s.setScoreFiles ({ a, b });
            const auto loaded = Session::fromVar (s.toVar());
            expectEquals (loaded.pdfBackingPath, b.getFullPathName());
        }
    }
};

static PdfScoreFilesTests pdfScoreFilesTests;
