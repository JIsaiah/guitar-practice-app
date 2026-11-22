#pragma once

#include <set>
#include "../AppSettings.h"
#include "SetlistCard.h"
#include "Icons.h"

// 一覧画面の「セットリスト」タブの中身 (setlist-mode.md 4.2、4.5)
// セットリストのカードを縦に並べる (スクロールできる)。右下に「＋」(新しいセットリスト。作者のレイアウト)
class SetlistPanel final : public juce::Component
{
public:
    SetlistPanel (SessionLibrary& library, SetlistStore& store, ThemeManager& theme, AppSettings& settings);

    void refresh();   // ファイルから読み直す

    std::function<void (const juce::String& setlistId, int entryIndex)> onPlay;
    std::function<void (const juce::String& sessionId)> onOpenSession;
    std::function<void (SetlistCard* card)> onRequestAdd;   // 追加パネルを開く (nullptr: 閉じる)

    SetlistCard* findCard (const juce::String& setlistId) const;
    SetlistCard* getFirstCard() const { return cards.empty() ? nullptr : cards.front().get(); }

    void resized() override;
    void paint (juce::Graphics& g) override;

    // キーボード操作 (一覧画面の矢印・Enter)
    const std::vector<std::unique_ptr<SetlistCard>>& getCards() const noexcept { return cards; }
    juce::Viewport& getViewport() noexcept { return viewport; }
    juce::Button& getNewButton() noexcept { return newButton; }

private:
    void createSetlist();
    void layoutCards();
    void removeCardLater (const juce::String& setlistId);

    SessionLibrary& library;
    SetlistStore& store;
    ThemeManager& theme;
    AppSettings& settings;

    juce::Viewport viewport;
    juce::Component content;

    // 「過去のセトリ」の区切り (作者のレイアウト: 真ん中に文字、左右に線)。ライブが終わったセットリストの前に出す
    class PastHeading final : public juce::Component
    {
    public:
        PastHeading() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override;
    };
    PastHeading pastHeading;
    IconButton newButton { Icons::plus };   // 右下の「＋」(セッションの一覧の「＋」と同じ見た目)
    std::vector<std::unique_ptr<SetlistCard>> cards;
    std::set<juce::String> expandedIds;   // 開いていたカード。設定に保存し、練習画面から戻ったときや次の起動でも開いたままにする
};
