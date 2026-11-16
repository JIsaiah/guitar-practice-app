#include "SessionListScreen.h"
#include "../Text.h"

// 一覧画面のキーボード操作 (作者の指示)
//   矢印: 選ぶ場所を、押した向きのいちばん近い場所へ動かす (SpatialNavigation)
//   Enter: 選んでいる場所を押す (セッションの行なら練習画面を開く)
//   Esc: 枠を消す
// 選んでいる場所はアクセントの色の枠で囲み、動かすと枠がなめらかに次の場所へ飛ぶ。
// マウスを押すと枠は消え、次に矢印を押すと、また出る (マウスで選んだ行があれば、そこから)

namespace
{
    SpatialNavigation::Box toBox (juce::Rectangle<int> r)
    {
        return { (float) r.getX(), (float) r.getY(), (float) r.getWidth(), (float) r.getHeight() };
    }

    void click (juce::Button& button)
    {
        if (button.isShowing() && button.isEnabled())
            button.triggerClick();
    }
}

//==============================================================================
std::vector<SessionListScreen::NavTarget> SessionListScreen::collectNavTargets()
{
    std::vector<NavTarget> targets;

    const auto addButton = [&] (const juce::String& id, juce::Button& button)
    {
        if (button.isShowing() && button.isEnabled())
            targets.push_back ({ id, getLocalArea (button.getParentComponent(), button.getBounds()), nullptr, true, {},
                                 [&button] { click (button); } });
    };

    // いちばん上: タブと設定
    targets.push_back ({ "tab:sessions", sessionsTab.getBounds(), nullptr, true, {}, [this] { showTab (Tab::sessions); } });
    targets.push_back ({ "tab:setlists", setlistsTab.getBounds(), nullptr, true, {}, [this] { showTab (Tab::setlists); } });
    addButton ("settings", settingsButton);

    if (currentTab == Tab::sessions)
    {
        // 左: フォルダ (Enter で絞り込む。矢印で通るだけでは一覧を変えない)
        const auto folderArea = getLocalArea (&folderList, folderList.getListArea());
        for (int i = 0; i < folderList.getNumItems(); ++i)
        {
            const auto bounds = getLocalArea (&folderList, folderList.getItemBounds (i));
            targets.push_back ({ "folder:" + folderList.getItemKey (i), bounds, &folderList, folderArea.intersects (bounds),
                                 [this, i] { folderList.scrollToItem (i); },
                                 [this, i] { folderList.chooseItem (i); } });
        }
        addButton ("folder:new", folderList.getNewButton());

        // 右: セッションの行 (選ぶと、その行を選んだ状態にする。Enter で練習画面を開く)
        const auto listArea = listBox.getBounds();
        for (int i = 0; i < (int) entries.size(); ++i)
        {
            const auto bounds = listBox.getRowPosition (i, true) + listBox.getPosition();
            targets.push_back ({ "row:" + entries[(size_t) i].id, bounds, &listBox, listArea.intersects (bounds),
                                 [this, i] { listBox.selectRow (i); },
                                 [this, i] { listBox.selectRow (i); openSelected(); } });
        }
        addButton ("new", newButton);
    }
    else
    {
        // セットリストのカード: 見出し (Enter で開く・閉じる)、「通しで再生」、開いていれば曲の行・「ここから再生」・「＋ 曲を追加」
        auto& viewport = setlistPanel.getViewport();
        const auto viewArea = getLocalArea (&viewport, viewport.getLocalBounds());

        for (const auto& cardPtr : setlistPanel.getCards())
        {
            auto* card = cardPtr.get();
            const auto id = "setlist:" + card->getSetlist().setlistId;
            const auto inCard = [this, card] (juce::Rectangle<int> r) { return getLocalArea (card, r); };
            const auto add = [&] (const juce::String& targetId, juce::Rectangle<int> bounds, std::function<void()> activate)
            {
                targets.push_back ({ targetId, bounds, &viewport, viewArea.intersects (bounds),
                                     [this, &viewport, bounds] { revealInViewport (viewport, bounds); }, std::move (activate) });
            };

            auto& play = card->getPlayButton();
            auto header = card->getHeaderArea();
            if (play.isVisible())
                header.setRight (play.getX() - 8);
            add (id + ":header", inCard (header), [card] { card->setExpanded (! card->isExpanded()); });

            if (play.isEnabled())
                add (id + ":play", inCard (play.getBounds()), [&play] { click (play); });

            if (! card->isExpanded())
                continue;

            auto& list = card->getEntryList();
            const auto inList = [this, &list] (juce::Rectangle<int> r) { return getLocalArea (&list, r); };
            for (int i = 0; i < list.getNumRows(); ++i)
            {
                const auto playArea = list.playButtonArea (i);
                auto row = list.getRowArea (i);
                row.setRight (playArea.getX() - 8);
                const auto entry = id + ":entry:" + juce::String (i);
                add (entry, inList (row), [&list, i] { list.openRow (i); });
                add (entry + ":play", inList (playArea), [&list, i] { list.playFromRow (i); });
            }

            add (id + ":add", inCard (SetlistEntryList::highlightArea (card->getAddRowBounds())), [card] { card->requestAdd(); });
        }
        addButton ("setlist:new", setlistPanel.getNewButton());

        // 曲の追加パネル (開いているとき): セッションの行 (Enter で追加) と「閉じる」
        if (picker.isVisible())
        {
            const auto pickArea = getLocalArea (&picker, picker.getListArea());
            for (int i = 0; i < picker.getNumItems(); ++i)
            {
                const auto bounds = getLocalArea (&picker, picker.getItemBounds (i));
                targets.push_back ({ "pick:" + juce::String (i), bounds, &picker, pickArea.intersects (bounds),
                                     [this, i] { picker.scrollToItem (i); },
                                     [this, i] { picker.addItem (i); } });
            }
            addButton ("pick:close", picker.getCloseButton());
        }
    }

    return targets;
}

void SessionListScreen::revealInViewport (juce::Viewport& viewport, juce::Rectangle<int> boundsInScreen)
{
    auto* content = viewport.getViewedComponent();
    if (content == nullptr)
        return;

    // 見えていなければ、上下に少し余白を付けて見えるところまでスクロールする
    // (下は、右下に重なっている「＋」の分も空ける)
    const auto inContent = content->getLocalArea (this, boundsInScreen);
    auto y = viewport.getViewPositionY();
    const auto height = viewport.getMaximumVisibleHeight();
    if (inContent.getY() - 8 < y)
        y = inContent.getY() - 8;
    else if (inContent.getBottom() + 56 > y + height)
        y = inContent.getBottom() + 56 - height;

    viewport.setViewPosition (viewport.getViewPositionX(), juce::jmax (0, y));
}

//==============================================================================
bool SessionListScreen::keyPressed (const juce::KeyPress& key)
{
    using SpatialNavigation::Direction;

    if (key == juce::KeyPress::upKey)    { moveNavigation (Direction::up);    return true; }
    if (key == juce::KeyPress::downKey)  { moveNavigation (Direction::down);  return true; }
    if (key == juce::KeyPress::leftKey)  { moveNavigation (Direction::left);  return true; }
    if (key == juce::KeyPress::rightKey) { moveNavigation (Direction::right); return true; }

    if (key == juce::KeyPress::returnKey)
    {
        // 枠が出ていないとき: セッションの行を選んでいれば開く (マウスで選んでから Enter)。無ければ枠を出す
        if (! navVisible)
        {
            if (currentTab == Tab::sessions && juce::isPositiveAndBelow (listBox.getSelectedRow(), (int) entries.size()))
                openSelected();
            else
                moveNavigation (Direction::down);
            return true;
        }

        const auto targets = collectNavTargets();
        for (const auto& target : targets)
        {
            if (target.id == navId && target.activate != nullptr)
            {
                const auto activate = target.activate;   // 練習画面を開くと、この画面は消える。最後に呼ぶ
                activate();
                return true;
            }
        }
        return true;
    }

    if (key == juce::KeyPress::escapeKey && navVisible)
    {
        hideNavigation();
        return true;
    }

    return false;
}

void SessionListScreen::keepKeyboardFocus()
{
    // ボタンや一覧は、マウスで押すとキーの入力を自分に移す (一覧は矢印で自分の行を動かし、ボタンは Enter で押される)。
    // 矢印と Enter はこの画面で扱うので、中の部品がキーの入力を取らないようにする (文字を打つ欄は除く)。
    // カードは読み直すと作り直されるので、マウスを押すたびに見直す
    std::function<void (juce::Component&)> release = [&release] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (dynamic_cast<juce::TextEditor*> (child) != nullptr)
                continue;

            child->setWantsKeyboardFocus (false);
            release (*child);
        }
    };
    release (*this);

    // すでに中の部品に移っていたら、この画面へ戻す。
    // (JUCE は、この画面から中の部品へ入力が移っても「この画面の中にある」ことは変わらないので、知らせを出さない。
    //  そのため、知らせ (focusOfChildComponentChanged) を待たずに、ここで確かめる)
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe == nullptr || ! safe->isShowing())
            return;

        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == safe.getComponent() || dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return;

        // 入力がどこにも無い、またはこの画面の中の部品にあるとき (ほかの窓・ダイアログにあるときは触らない)
        if (focused == nullptr || safe->isParentOf (focused))
            safe->grabKeyboardFocus();
    });
}

void SessionListScreen::moveNavigation (SpatialNavigation::Direction direction)
{
    const auto targets = collectNavTargets();
    if (targets.empty())
        return;

    const auto find = [&targets] (const juce::String& id)
    {
        return std::find_if (targets.begin(), targets.end(), [&id] (const NavTarget& t) { return t.id == id; });
    };

    // 枠が出ていないとき: まず枠を出すだけ (動かさない)。場所は、前に選んでいた場所 → 選んでいる行 → 見えているいちばん上
    if (! navVisible || find (navId) == targets.end())
    {
        auto start = find (navId);
        if (start == targets.end() && currentTab == Tab::sessions && juce::isPositiveAndBelow (listBox.getSelectedRow(), (int) entries.size()))
            start = find ("row:" + entries[(size_t) listBox.getSelectedRow()].id);
        if (start == targets.end())
            start = std::find_if (targets.begin(), targets.end(), [] (const NavTarget& t) { return t.visible; });
        if (start != targets.end())
            selectNavTarget (*start);
        return;
    }

    const auto& current = *find (navId);

    // 候補: 見えている場所。今と同じ入れ物の中なら、スクロールして見えていない場所も (下の行へ進むとスクロールする)
    std::vector<SpatialNavigation::Box> boxes;
    std::vector<const NavTarget*> candidates;
    for (const auto& target : targets)
    {
        if (target.id == navId)
            continue;
        if (! target.visible && (current.group == nullptr || target.group != current.group))
            continue;

        boxes.push_back (toBox (target.bounds));
        candidates.push_back (&target);
    }

    const auto next = SpatialNavigation::findNext (boxes, toBox (current.bounds), direction);
    if (next >= 0)
        selectNavTarget (*candidates[(size_t) next]);
}

std::optional<juce::Rectangle<float>> SessionListScreen::findNavTargetBounds()
{
    for (const auto& t : collectNavTargets())
        if (t.id == navId)
            return t.bounds.toFloat();

    return std::nullopt;
}

void SessionListScreen::selectNavTarget (const NavTarget& target)
{
    navId = target.id;
    if (target.reveal != nullptr)
        target.reveal();

    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (! navVisible)
    {
        // 枠が出ていなかったときは、その場所にふわっと出す (飛んでこない)
        navVisible = true;
        navShownAtMs = now;
        navRing = findNavTargetBounds().value_or (target.bounds.toFloat());   // スクロールしたあとの場所
        navMoveFrom = navRing;
        navMoveStartMs = now - navMoveDurationMs;   // 動きは無し
    }
    else
    {
        // 今描いている場所から、新しい場所へ飛ぶ (途中で次を押したら、その途中の場所から)
        navMoveFrom = navRing;
        navMoveStartMs = now;
    }

    // 画面の書き換えの合図 (60 回/秒・144 回/秒など、画面に合わせた間隔) ごとに、経過時間から枠の場所を決める
    ensureFrameVBlank();
    stepNavigationRing (now);
}

void SessionListScreen::hideNavigation()
{
    if (! navVisible && navId.isEmpty())
        return;

    if (navVisible)
        repaint (navRing.getSmallestIntegerContainer().expanded (12));

    navVisible = false;
    navId = {};   // 次は、マウスで選んだ行から始める
    // 画面の書き換えの合図 (frameVBlank) は残しておく (合図の中から呼ばれることがあり、そこで消すと危ないため。枠が無い間は何もしない)
}

void SessionListScreen::stepNavigationRing (double now)
{
    if (! navVisible)
        return;

    // 目標の場所 (スクロールで動くので、毎回求める)。無くなっていたら (読み直し・タブの切り替え)、枠を消す
    const auto goal = findNavTargetBounds();
    if (! goal.has_value())
    {
        hideNavigation();
        return;
    }

    // now は、合図が渡す書き換えの時刻 (onFrame)。呼ばれた時刻ではないので、合図が届くまでの遅れがばらついても、動きは揺れない
    const auto before = navRing;

    // 経過時間で決める (はじめ速く、着く前にゆっくり: 3 次のイーズアウト)。間隔が揺れても、動きはなめらか
    const auto t = (float) juce::jlimit (0.0, 1.0, (now - navMoveStartMs) / navMoveDurationMs);
    const auto eased = 1.0f - std::pow (1.0f - t, 3.0f);
    const auto lerp = [eased] (float a, float b) { return a + (b - a) * eased; };
    navRing = { lerp (navMoveFrom.getX(), goal->getX()), lerp (navMoveFrom.getY(), goal->getY()),
                lerp (navMoveFrom.getWidth(), goal->getWidth()), lerp (navMoveFrom.getHeight(), goal->getHeight()) };
    navAlpha = (float) juce::jlimit (0.0, 1.0, (now - navShownAtMs) / 150.0);

    if (navRing != before || navAlpha < 1.0f || t < 1.0f)
    {
        repaint (before.getSmallestIntegerContainer().expanded (8));
        repaint (navRing.getSmallestIntegerContainer().expanded (8));
    }
}

void SessionListScreen::paintNavigationRing (juce::Graphics& g)
{
    if (! navVisible)
        return;

    // アクセントの色の細い枠と、外側のうっすらした光 (作者の指示で細くした)
    const auto accent = findColour (ThemeColourIds::accent).withMultipliedAlpha (navAlpha);
    const auto ring = navRing.expanded (2.0f);
    g.setColour (accent.withMultipliedAlpha (0.18f));
    g.drawRoundedRectangle (ring.expanded (1.5f), 9.5f, 2.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (ring, 8.0f, 1.5f);
}

void SessionListScreen::showKeyboardNavigationForSnapshot()
{
    // 枠を出し (選んでいる行)、下 → 右 → 下 と動かして、セッションの3行目に枠がある状態にする
    using SpatialNavigation::Direction;
    for (auto direction : { Direction::down, Direction::down, Direction::right, Direction::down })
        moveNavigation (direction);

    // 動きを終わらせる
    navMoveStartMs -= navMoveDurationMs;
    navShownAtMs -= 1000.0;
    stepNavigationRing (juce::Time::getMillisecondCounterHiRes());
    frameVBlank.reset();
}
