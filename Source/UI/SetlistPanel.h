#pragma once

#include <set>
#include "../AppSettings.h"
#include "SetlistCard.h"

// 一覧画面の「セットリスト」タブの中身 (setlist-mode.md 4.2、4.5)
// 先頭に「新しいセットリスト」、その下にセットリストのカードを縦に並べる (スクロールできる)
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
    juce::TextButton newButton;
    std::vector<std::unique_ptr<SetlistCard>> cards;
    std::set<juce::String> expandedIds;   // 開いていたカード。設定に保存し、練習画面から戻ったときや次の起動でも開いたままにする
};
