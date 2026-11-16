#include <juce_core/juce_core.h>
#include "../Source/Library/SpatialNavigation.h"

// 一覧画面のキーボード操作: 矢印の向きで次の場所を選ぶ
class SpatialNavigationTests final : public juce::UnitTest
{
public:
    SpatialNavigationTests() : juce::UnitTest ("SpatialNavigation", "Library") {}

    void runTest() override
    {
        using SpatialNavigation::Box;
        using SpatialNavigation::Direction;
        using SpatialNavigation::findNext;

        // 画面の形: 上にタブ2つと歯車、左にフォルダ2つ、右に一覧の行3つ
        const Box tabSessions { 0, 0, 400, 50 }, tabSetlists { 400, 0, 400, 50 }, settings { 820, 0, 50, 50 };
        const Box folderAll { 0, 100, 200, 40 }, folderRock { 0, 140, 200, 40 };
        const Box row0 { 220, 100, 650, 40 }, row1 { 220, 140, 650, 40 }, row2 { 220, 180, 650, 40 };
        const std::vector<Box> boxes { tabSessions, tabSetlists, settings, folderAll, folderRock, row0, row1, row2 };

        beginTest ("Up and down move one row at a time");
        {
            expectEquals (findNext (boxes, row0, Direction::down), 6);
            expectEquals (findNext (boxes, row2, Direction::up), 6);
            expectEquals (findNext (boxes, row2, Direction::down), -1);   // 下には何もない
        }

        beginTest ("Left and right go to the item at the same height");
        {
            expectEquals (findNext (boxes, row1, Direction::left), 4);    // 同じ高さのフォルダ
            expectEquals (findNext (boxes, folderAll, Direction::right), 5);
            expectEquals (findNext (boxes, tabSessions, Direction::right), 1);
            expectEquals (findNext (boxes, tabSetlists, Direction::right), 2);
        }

        beginTest ("Up from the list reaches the tab above it");
        {
            expectEquals (findNext (boxes, row0, Direction::up), 1);      // 一覧の真ん中の上はセットリストのタブ
            expectEquals (findNext (boxes, folderAll, Direction::up), 0);
            expectEquals (findNext (boxes, tabSessions, Direction::down), 3);
        }

        beginTest ("A box whose centre is not past the current edge is skipped");
        {
            const Box wide { 0, 0, 800, 100 }, inside { 600, 40, 100, 20 }, beside { 820, 40, 40, 20 };
            expectEquals (findNext ({ wide }, inside, Direction::right), -1);
            expectEquals (findNext ({ inside }, wide, Direction::right), -1);
            expectEquals (findNext ({ inside, beside }, wide, Direction::right), 1);
            expectEquals (findNext (boxes, row1, Direction::right), -1);   // 横に長い行の「右」: 右上のタブ・歯車へは飛ばない
        }
    }
};

static SpatialNavigationTests spatialNavigationTests;
