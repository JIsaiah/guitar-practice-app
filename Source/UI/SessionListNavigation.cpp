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
        {
            // スクロールする入れ物 (セッションの一覧・セットリストの欄) の中なら、その入れ物の見えている範囲だけに描く
            // (作者の報告: 下へスクロールすると、枠が一覧の外の「セッション｜セットリスト」の後ろまで出ていた)
            navClip = t.group != nullptr ? getLocalArea (t.group->getParentComponent(), t.group->getBounds()).toFloat()
                                         : juce::Rectangle<float>();
            return t.bounds.toFloat();
        }

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
    updateKeyboardHighlights();
    stepNavigationRing (now);
    repaint (navRing.getSmallestIntegerContainer().expanded (8));   // 出したばかりのとき (場所が変わらなくても描く)
}

void SessionListScreen::hideNavigation()
{
    if (! navVisible && navId.isEmpty())
        return;

    if (navVisible)
        repaint (navRing.getSmallestIntegerContainer().expanded (12));

    navVisible = false;
    navId = {};   // 次は、マウスで選んだ行から始める
    updateKeyboardHighlights();   // 選んでいるフォルダ・行の塗りを戻す
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

    // 枠は動かさず、すぐに選んだ場所に置き、すぐに濃く出す (作者の指示: 飛んでいく動きが、選んだ行の塗りに追いつかず、
    // ずれて見えたため、動きをやめた)。スクロールで行が動いたときは、毎回その場所に合わせる
    juce::ignoreUnused (now);
    navRing = *goal;
    navAlpha = 1.0f;

    if (navRing != before)
    {
        repaint (before.getSmallestIntegerContainer().expanded (8));
        repaint (navRing.getSmallestIntegerContainer().expanded (8));
    }
}

void SessionListScreen::paintNavigationRing (juce::Graphics& g)
{
    if (! navVisible)
        return;

    // スクロールする入れ物の中の枠は、入れ物の外に描かない (枠の外側の光の分だけ広げる)
    juce::Graphics::ScopedSaveState saved (g);
    if (! navClip.isEmpty())
        g.reduceClipRegion (navClip.expanded (4.0f).getSmallestIntegerContainer());

    // 枠は描かない (作者の指示: 後ろのハイライトだけにする)。
    // セッションの行・フォルダは、一覧が自分で塗る (文字の後ろに。updateKeyboardHighlights)。
    // それ以外 (タブ・ボタン・セットリストのカードなど) は、一覧の選んだ行と同じ色の、薄い塗りを重ねる
    if (navId.startsWith ("row:") || (navId.startsWith ("folder:") && navId != "folder:new"))
        return;

    AppLookAndFeel::fillListHighlight (g, navRing.getSmallestIntegerContainer(),
                                       findColour (ThemeColourIds::listSelection).withMultipliedAlpha (0.55f * navAlpha));
}

void SessionListScreen::updateKeyboardHighlights()
{
    // フォルダ: キーボードで操作していない → 選んでいるフォルダを塗る。操作中 → その場所のフォルダだけ (無ければどれも塗らない)
    int folderIndex = -2;
    if (navVisible)
    {
        folderIndex = -1;
        for (int i = 0; i < folderList.getNumItems(); ++i)
            if (navId == "folder:" + folderList.getItemKey (i))
                folderIndex = i;
    }
    folderList.setKeyboardHighlight (folderIndex);

    // セッションの行: paintListBoxItem が navVisible・navId を見て塗る
    listBox.repaint();
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

void SessionListScreen::showNavigationOnFirstSession (int attempts)
{
    // 起動した直後は、ウィンドウがまだ出ていない・大きさが決まっていない (行の場所が分からない) ので、出るまで待つ
    juce::Timer::callAfterDelay (50, [safe = juce::Component::SafePointer (this), attempts]
    {
        if (safe == nullptr)
            return;

        if (! safe->isShowing() || safe->listBox.getHeight() <= 0)
        {
            if (attempts > 0)
                safe->showNavigationOnFirstSession (attempts - 1);
            return;
        }

        // セッションのタブで、セッションがあり、まだ自分で枠を動かしていないときだけ
        if (safe->currentTab != Tab::sessions || safe->entries.empty() || safe->navVisible)
            return;

        const auto firstId = "row:" + safe->entries.front().id;
        for (const auto& target : safe->collectNavTargets())
            if (target.id == firstId)
            {
                safe->selectNavTarget (target);
                break;
            }

        safe->grabKeyboardFocus();   // Enter・矢印を、この画面で受け取る
    });
}
