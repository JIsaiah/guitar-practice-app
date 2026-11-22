#pragma once

#include "../Library/SetlistStore.h"
#include "../Theme/MetalLogo/FixedTextButton.h"
#include "SetlistEntryList.h"
#include "FoldAnimation.h"
#include "Icons.h"

// セットリストのカード (setlist-mode.md 4.2〜4.5)
// - 見出し: 名前、曲数、合計時間、「通しで再生」(1曲目から)
// - 見出しのクリックで展開し (▲ 開いている / ▼ 閉じている)、曲順 (SetlistEntryList) と「＋ 曲を追加」の行を出す
// - 見出しの右クリック: 名前の変更、削除 (中のセッションは消さない)
// 変更はすぐにファイルへ保存する
class SetlistCard final : public juce::Component
{
public:
    SetlistCard (Setlist setlist, SessionLibrary& library, SetlistStore& store, ThemeManager& theme);

    const Setlist& getSetlist() const noexcept { return setlist; }

    bool isExpanded() const noexcept { return expanded; }
    void setExpanded (bool shouldBeExpanded);
    int getPreferredHeight() const;

    // 末尾に1曲追加して保存する (追加パネルから)
    void addSession (const juce::String& sessionId);
    void setAdding (bool isAdding);   // 追加パネルを開いている間、「＋ 曲を追加」をアクセントの色にする

    std::function<void()> onLayoutChanged;                            // 高さが変わった
    std::function<void (int entryIndex)> onPlay;                      // その曲から始める
    std::function<void (const juce::String& sessionId)> onOpenSession; // 単体で開く
    std::function<void()> onRequestAdd;                               // 「＋ 曲を追加」
    std::function<void()> onDeleted;                                  // セットリストを削除した
    std::function<void()> onFinishedChanged;                          // 「ライブ終わった」を付けた・外した (一覧が並べ直す)

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    static constexpr int headerHeight = 72, addRowHeight = 46;

    // キーボード操作 (一覧画面の矢印・Enter)
    juce::Rectangle<int> getHeaderArea() const { return getLocalBounds().withHeight (headerHeight); }
    juce::Rectangle<int> getNameArea() const;   // 見出しの、名前の変更 (ダブルクリック) が効く場所
    juce::Rectangle<int> getAddRowBounds() const { return getAddRowArea(); }   // 開いていないときは空
    juce::Button& getPlayButton() noexcept { return playButton; }
    SetlistEntryList& getEntryList() noexcept { return entryList; }
    void requestAdd() { if (onRequestAdd != nullptr) onRequestAdd(); }

private:
    void save();
    void changed();   // 曲の追加・削除・並び替えのあと
    void updatePlayButton();   // 開ける曲が無ければ「通しで再生」を押せなくする
    void showHeaderMenu();
    void rename();
    void editLiveDate();   // ライブの日を変える (作者の指示。空にすると、決めていないに戻る)
    void setFinished (bool shouldBeFinished);
    void updateAlpha();   // ライブが終わったもの: たたんでいる間は 50%、開いている間は 100%
    void requestDelete();
    juce::String describeSummary();   // 「8曲・34:12」

    Setlist setlist;
    SessionLibrary& library;
    SetlistStore& store;

    juce::Rectangle<int> getAddRowArea() const;   // 「＋ 曲を追加」の行 (開いているときだけ)

    bool expanded = false, adding = false, addHover = false;
    FixedTextButton playButton;
    IconButton menuButton { Icons::menuHorizontal };   // 「通しで再生」の右の「…」。右クリックと同じメニュー (作者の指示)
    SetlistEntryList entryList;
    FoldAnimation openFold { *this };   // 開く・たたむ動き (作者の指示: 上へたたむ)。0 = たたんだ、1 = 開いた
};
