#include "SetlistPanel.h"
#include "../Text.h"

namespace
{
    constexpr int cardGap = 12;
    constexpr int newButtonSize = 44;   // 右下の「＋」(正方形)
}

SetlistPanel::SetlistPanel (SessionLibrary& l, SetlistStore& s, ThemeManager& t, AppSettings& a)
    : library (l), store (s), theme (t), settings (a)
{
    for (const auto& id : settings.getExpandedSetlists())
        expandedIds.insert (id);

    newButton.setTooltip ("新しいセットリスト"_jp);
    newButton.onClick = [this] { createSetlist(); };

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (newButton);   // カードの上に重ねて、右下に置く (スクロールしても動かない)

    refresh();
}

void SetlistPanel::refresh()
{
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
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
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
    // 開閉はここを通るので、開いているカードを覚えておく (消したセットリストは自然に外れる)
    juce::StringArray ids;
    expandedIds.clear();
    for (const auto& card : cards)
        if (card->isExpanded())
        {
            ids.add (card->getSetlist().setlistId);
            expandedIds.insert (card->getSetlist().setlistId);
        }

    ids.sort (false);
    auto saved = settings.getExpandedSetlists();
    saved.sort (false);
    if (ids != saved)
        settings.setExpandedSetlists (ids);

    const auto width = viewport.getMaximumVisibleWidth();
    int y = 0;

    for (const auto& card : cards)
    {
        const auto height = card->getPreferredHeight();
        card->setBounds (0, y, width, height);
        y += height + cardGap;
    }

    // いちばん下のカードが「＋」に隠れないよう、下に「＋」の分の余白を足す
    content.setSize (width, juce::jmax (y + newButtonSize + 8, viewport.getHeight()));
}

void SetlistPanel::paint (juce::Graphics& g)
{
    if (! cards.empty())
        return;

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    g.drawFittedText ("セットリストがありません。\n右下の「＋」から作成してください。"_jp,
                      getLocalBounds().withHeight (160), juce::Justification::centred, 2);
}

void SetlistPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    newButton.setBounds (getLocalBounds().removeFromBottom (newButtonSize + 8).removeFromRight (newButtonSize + 8)
                             .removeFromTop (newButtonSize).removeFromLeft (newButtonSize));
    layoutCards();
}
