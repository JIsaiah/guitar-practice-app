#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Library/SessionLibrary.h"
#include "../Library/Setlist.h"

// セットリストを展開したときの曲順の一覧 (setlist-mode.md 4.3)
// - 行のクリック: その曲のセッションを単体で開く (作者の指示。仕様書の「その曲からセットリストを始める」は右クリックへ)
// - 行の右の「▶ ここから再生」: その曲からセットリストを通しで再生する (作者のレイアウト)
// - ドラッグ: 並び替え
// - 右クリック: 「この曲から通しで再生」「セットリストから外す」「元のセッションを単体で開く」
// - 参照先のセッションが無い行 (リンク切れ) はグレーで「見つかりません」
class SetlistEntryList final : public juce::Component
{
public:
    SetlistEntryList (Setlist& setlist, SessionLibrary& library);

    static constexpr int rowHeight = 46;
    static constexpr int numberWidth = 44;   // 番号の列の幅 (カードの「＋ 曲を追加」もこの列にそろえる)
    int getPreferredHeight() const { return (int) setlist.entries.size() * rowHeight; }

    std::function<void (int entryIndex)> onPlay;
    std::function<void()> onChanged;   // 並び替え・削除をした (保存と配置のし直し)
    std::function<void (const juce::String& sessionId)> onOpenSession;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    int rowAt (int y) const;
    juce::Rectangle<int> playButtonArea (int row) const;   // 行の右の「▶ ここから再生」
public:
    static juce::Rectangle<int> highlightArea (juce::Rectangle<int> row);   // 行の塗りの範囲 (カードの「＋ 曲を追加」も同じ)
private:
    int insertIndexAt (int y) const;   // ドラッグ中、離したら入る位置 (行の境目。0〜行数)
    void showRowMenu (int row);

    Setlist& setlist;
    SessionLibrary& library;
    int hoverRow = -1, pressedRow = -1, insertIndex = -1;
    bool hoverOnPlay = false;
    bool dragging = false;
};

// 曲の長さの表示 ("3:25"、1時間以上は "1:02:03")
juce::String formatDuration (double seconds);

// 専用設定が初期値から変わっていれば、その印 ("速度80%")。初期値なら空。ループは出さない (作者の指示)
juce::String describeOverrides (const SetlistEntry::Overrides& overrides);
