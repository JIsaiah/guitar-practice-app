#include "SetlistPanel.h"
#include "../Text.h"

namespace
{
    constexpr int cardGap = 12;
    constexpr int newButtonHeight = 56;
}

SetlistPanel::SetlistPanel (SessionLibrary& l, SetlistStore& s, ThemeManager& t)
    : library (l), store (s), theme (t)
{
    newButton.setButtonText ("＋ 新しいセットリスト"_jp);
    newButton.onClick = [this] { createSetlist(); };
    content.addAndMakeVisible (newButton);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    refresh();
}

void SetlistPanel::refresh()
{
    for (const auto& card : cards)
        if (card->isExpanded())
            expandedIds.insert (card->getSetlist().setlistId);

    cards.clear();

    for (auto& setlist : store.list())
    {
        const auto id = setlist.setlistId;
        auto card = std::make_unique<SetlistCard> (std::move (setlist), library, store, theme);

        card->setExpanded (expandedIds.count (id) > 0);
        card->onLayoutChanged = [this] { layoutCards(); };
        card->onPlay = [this, id] (int index) { if (onPlay != nullptr) onPlay (id, index); };
        card->onOpenSession = [this] (const juce::String& sessionId) { if (onOpenSession != nullptr) onOpenSession (sessionId); };
        card->onRequestAdd = [this, raw = card.get()] { if (onRequestAdd != nullptr) onRequestAdd (raw); };
        card->onDeleted = [this, id] { removeCardLater (id); };

        content.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }

    layoutCards();
}

SetlistCard* SetlistPanel::findCard (const juce::String& setlistId) const
{
    for (const auto& card : cards)
        if (card->getSetlist().setlistId == setlistId)
            return card.get();

    return nullptr;
}

void SetlistPanel::createSetlist()
{
    auto setlist = store.createNew();
    if (! store.save (setlist))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "保存できませんでした"_jp,
                                                store.getDirectory().getFullPathName());
        return;
    }

    // 作ったものは開いた状態で先頭に出し、すぐに追加パネルを開く (名前はあとから変えられる)
    expandedIds.insert (setlist.setlistId);
    refresh();
    viewport.setViewPosition (0, 0);

    if (auto* card = findCard (setlist.setlistId); card != nullptr && onRequestAdd != nullptr)
        onRequestAdd (card);
}

void SetlistPanel::removeCardLater (const juce::String& setlistId)
{
    // カード自身のコールバックの中から呼ばれるので、カードの破棄は次のメッセージで
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this), setlistId]
    {
        if (safe == nullptr)
            return;

        if (auto* card = safe->findCard (setlistId); card != nullptr && safe->onRequestAdd != nullptr)
            safe->onRequestAdd (nullptr);   // 追加パネルがこのカードを指していたら閉じる

        safe->expandedIds.erase (setlistId);
        safe->refresh();
    });
}

void SetlistPanel::layoutCards()
{
    const auto width = viewport.getMaximumVisibleWidth();
    int y = 0;

    newButton.setBounds (0, y, width, newButtonHeight);
    y += newButtonHeight + cardGap;

    for (const auto& card : cards)
    {
        const auto height = card->getPreferredHeight();
        card->setBounds (0, y, width, height);
        y += height + cardGap;
    }

    content.setSize (width, juce::jmax (y, viewport.getHeight()));
}

void SetlistPanel::paint (juce::Graphics& g)
{
    if (! cards.empty())
        return;

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    g.drawFittedText ("セットリストがありません。\n「＋ 新しいセットリスト」から作成してください。"_jp,
                      getLocalBounds().withTrimmedTop (newButtonHeight + cardGap).withHeight (120),
                      juce::Justification::centred, 2);
}

void SetlistPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutCards();
}
