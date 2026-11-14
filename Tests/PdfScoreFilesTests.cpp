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

        beginTest ("PDF repeats: overlaps replaced, sorted, saved per PDF");
        {
            Session s;
            s.setScoreFiles ({ a, b });
            auto& lead = s.pdfRepeats;
            expect (Session::addPdfRepeat (lead, { 1.5, 2.2, 3 }));
            expect (Session::addPdfRepeat (lead, { 0.2, 0.8, 2 }));
            expect (! Session::addPdfRepeat (lead, { 0.9, 0.9, 2 }));   // 始め = 終わりは置かない
            expectEquals ((int) lead.size(), 2);
            expectEquals (lead[0].start, 0.2);                           // 始めの順
            expect (Session::addPdfRepeat (lead, { 2.0, 2.5, 99 }));    // 1.5〜2.2 と重なる → 置き換え。回数は 8 まで
            expectEquals ((int) lead.size(), 2);
            expectEquals (lead[1].start, 2.0);
            expectEquals (lead[1].count, Session::maxPdfRepeatCount);

            Session::addPdfRepeat (s.pdfBackingRepeats, { 0.1, 0.4, 4 });
            s.backingActive = true;
            expectEquals ((int) s.currentPdfRepeats().size(), 1);         // バッキングの PDF の分

            const auto loaded = Session::fromVar (s.toVar());
            expectEquals ((int) loaded.pdfRepeats.size(), 2);
            expectEquals (loaded.pdfRepeats[1].end, 2.5);
            expectEquals ((int) loaded.pdfBackingRepeats.size(), 1);
            expectEquals (loaded.pdfBackingRepeats[0].count, 4);

            s.setScoreFiles ({ a });                                      // 別の PDF を選んだら消える
            expect (s.pdfRepeats.empty() && s.pdfBackingRepeats.empty());
        }
    }
};

static PdfScoreFilesTests pdfScoreFilesTests;
