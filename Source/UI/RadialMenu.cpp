#include "RadialMenu.h"
#include "../Platform/MouseState.h"
#include "../Platform/WindowOwner.h"
#include "../Theme/DeathMetalLookAndFeel.h"
#include "../Theme/Palette.h"
#include "../Text.h"

namespace
{
    constexpr float maxRadius = 324.0f;      // 作者の指示で 20% 大きく (270 → 324)
    constexpr float margin = 12.0f;           // 円と画面の端のすき間
    constexpr float deadZone = 0.17f;         // 中心のこの割合の内側では、何も選ばない
    constexpr float hoverGrow = 0.10f;        // 選んだ扇形が外に伸びる割合
    constexpr double openSeconds = 0.16;      // 開くアニメーションの長さ

    // 角度: 上を 0 として時計回り (JUCE の Path::addPieSegment と同じ向き)
    float angleOf (juce::Point<float> v)
    {
        auto a = std::atan2 (v.x, -v.y);
        if (a < 0.0f)
            a += juce::MathConstants<float>::twoPi;
        return a;
    }
}

//==============================================================================
RadialMenu::RadialMenu (juce::Component& t, std::function<std::vector<Item>()> make)
    : target (t), makeItems (std::move (make))
{
    startTimerHz (60);
}

RadialMenu::~RadialMenu()
{
    stopTimer();
    window = nullptr;
    if (suppressed != nullptr)
        suppressed->setEnabled (true);
}

bool RadialMenu::canStartAt (juce::Point<int> screenPos) const
{
    if (! target.isShowing() || ! juce::Process::isForegroundProcess())
        return false;

    // ダイアログやポップアップメニューが出ている間は開かない
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        return false;

    if (! target.getScreenBounds().contains (screenPos))
        return false;

    // カーソルの下が、アプリのウィンドウであること (プラグインのウィンドウなどの上では開かない)。
    // タブ譜・YouTube の WebView は、練習画面ではなく MainComponent の子なので、同じウィンドウかで見る
    auto* under = juce::Desktop::getInstance().findComponentAt (screenPos);
    return under != nullptr && under->getTopLevelComponent() == target.getTopLevelComponent();
}

void RadialMenu::timerCallback()
{
    const bool left = MouseState::isLeftDown(), right = MouseState::isRightDown(), middle = MouseState::isMiddleDown();

    // 開く操作: 左右の同時押し、または真ん中のボタン (ホイールの押し込み) を押し続ける。
    // 左右の同時押しはトラックボールでは難しいので、真ん中のボタンでも開けるようにした (作者の指示)
    const bool pressed = (left && right) || middle;
    const bool anyDown = left || right || middle;
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    const auto now = juce::Time::getMillisecondCounterHiRes();

    switch (phase)
    {
        case Phase::idle:
            if (pressed && canStartAt (mouse))
            {
                phase = Phase::holding;
                holdStartMs = now;
            }
            break;

        case Phase::holding:
            if (! pressed)
                phase = anyDown ? Phase::waitRelease : Phase::idle;   // 出るより前に離した
            else if (now - holdStartMs >= holdMs)
                show (mouse);
            break;

        case Phase::open:
        {
            if (window == nullptr)
            {
                phase = Phase::waitRelease;
                break;
            }

            const auto local = (mouse - target.getScreenPosition()).toFloat();
            window->updateHover (local);

            // ボタンを離したら (左右ならどちらか、真ん中ならそれ)、選んでいる操作を実行して閉じる
            if (! pressed)
            {
                close (true);
                break;
            }

            if (window->advance (1.0 / 60.0))
                window->repaint();
            break;
        }

        case Phase::waitRelease:
            // すべてのボタンを離すまでは、次を始めない。離したら、使えなくしていた部品を戻す
            if (! anyDown)
            {
                phase = Phase::idle;
                if (suppressed != nullptr)
                {
                    // 離したときの知らせ (mouseUp) が部品に届いたあとで戻す
                    juce::MessageManager::callAsync ([c = suppressed] { if (c != nullptr) c->setEnabled (true); });
                    suppressed = nullptr;
                }
            }
            break;
    }
}

void RadialMenu::show (juce::Point<int> screenPos)
{
    auto items = makeItems != nullptr ? makeItems() : std::vector<Item>();
    if (items.empty())
    {
        phase = Phase::waitRelease;
        return;
    }

    // 押し始めたところの部品 (ボタンなど) を、離すまで使えなくする (離したときに、それも押されないように)
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    if (source.isDragging())
        if (auto* c = source.getComponentUnderMouse(); c != nullptr && c->isEnabled()
                                                       && c->getTopLevelComponent() == target.getTopLevelComponent())
        {
            suppressed = c;
            c->setEnabled (false);
        }

    window = std::make_unique<Window> (std::move (items));
    window->setBounds (target.getScreenBounds());

    // 枠のない、クリックを下に通すウィンドウ。アプリのウィンドウを持ち主にして、その手前 (WebView より手前) に出す
    window->addToDesktop (juce::ComponentPeer::windowIgnoresMouseClicks | juce::ComponentPeer::windowIsTemporary);
    if (auto* top = target.getTopLevelComponent(); top != nullptr && top->isOnDesktop())
        WindowOwner::setOwner (window->getWindowHandle(), top->getWindowHandle());

    window->placeAround (screenPos - target.getScreenPosition());
    window->updateHover ((screenPos - target.getScreenPosition()).toFloat());
    window->setVisible (true);
    window->toFront (false);   // キーボードの入力は、アプリのウィンドウのまま

    phase = Phase::open;
    if (onOpenChanged != nullptr)
        onOpenChanged (true);
}

void RadialMenu::close (bool runAction)
{
    std::function<void()> action;

    if (window != nullptr && runAction)
    {
        const auto index = window->getHovered();
        const auto& items = window->getItems();
        if (juce::isPositiveAndBelow (index, (int) items.size()) && items[(size_t) index].enabled)
            action = items[(size_t) index].action;
    }

    window = nullptr;
    phase = Phase::waitRelease;
    if (onOpenChanged != nullptr)
        onOpenChanged (false);

    // 実行は次のメッセージで (このタイマーの処理の途中で、画面を変えるような操作をしないように)
    if (action != nullptr)
        juce::MessageManager::callAsync (std::move (action));
}

//==============================================================================
RadialMenu::Window::Window (std::vector<Item> newItems)
    : items (std::move (newItems)), hoverAmounts (items.size(), 0.0f)
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void RadialMenu::Window::placeAround (juce::Point<int> mousePos)
{
    const auto bounds = getLocalBounds().toFloat();

    // 選んだ扇形は外に伸びる (hoverGrow) ので、伸びた分も画面に収まる大きさ・位置にする
    const auto grown = 1.0f + hoverGrow;
    radius = juce::jmin (maxRadius, (juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - margin) / grown);

    // カーソルが中心。はみ出るときは、はみ出ない範囲でカーソルに一番近いところ
    const auto limits = bounds.reduced (radius * grown + margin);
    centre = { juce::jlimit (limits.getX(), juce::jmax (limits.getX(), limits.getRight()), (float) mousePos.x),
               juce::jlimit (limits.getY(), juce::jmax (limits.getY(), limits.getBottom()), (float) mousePos.y) };
}

void RadialMenu::Window::updateHover (juce::Point<float> mousePos)
{
    const auto v = mousePos - centre;
    int index = -1;

    if (! items.empty() && v.getDistanceFromOrigin() > radius * deadZone)
    {
        // 扇形 i は、角度 i × (360 / n) を真ん中にして、その前後半分ずつ
        const auto step = juce::MathConstants<float>::twoPi / (float) items.size();
        index = (int) std::floor ((angleOf (v) + step * 0.5f) / step) % (int) items.size();
    }

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

bool RadialMenu::Window::advance (double dt)
{
    bool moving = false;

    if (openAmount < 1.0f)
    {
        openAmount = juce::jmin (1.0f, openAmount + (float) (dt / openSeconds));
        moving = true;
    }

    // 選ばれ具合は、目標 (選んでいれば 1) に向かって、毎コマ 3割ずつ近づける (なめらかに止まる)
    for (size_t i = 0; i < hoverAmounts.size(); ++i)
    {
        const auto targetAmount = (int) i == hovered ? 1.0f : 0.0f;
        auto& a = hoverAmounts[i];
        if (std::abs (targetAmount - a) > 0.002f)
        {
            a += (targetAmount - a) * 0.3f;
            moving = true;
        }
        else
        {
            a = targetAmount;
        }
    }

    return moving;
}

#if JUCE_DEBUG
void RadialMenu::Window::finishAnimationForTest()
{
    openAmount = 1.0f;
    for (size_t i = 0; i < hoverAmounts.size(); ++i)
        hoverAmounts[i] = (int) i == hovered ? 1.0f : 0.0f;
}
#endif

juce::Path RadialMenu::Window::makeSector (int index, float outerRadius) const
{
    const auto step = juce::MathConstants<float>::twoPi / (float) items.size();
    const auto from = (float) index * step - step * 0.5f;

    juce::Path p;
    p.addPieSegment (centre.x - outerRadius, centre.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f,
                     from, from + step, 0.0f);
    return p;
}

void RadialMenu::Window::paint (juce::Graphics& g)
{
    // 見た目は、アプリのほかの部品 (カード・ボタン) に合わせる (作者の指示):
    //   扇形 = 面の色 (カードと同じ)、区切りと外周 = 枠線の色、文字 = ふつうの文字の色。
    //   選んでいる扇形だけ、アクセントの色 (選ばれている音色のボタンと同じ)
    const auto ease = 1.0f - std::pow (1.0f - openAmount, 3.0f);   // 最初は速く、最後はゆっくり
    g.fillAll (juce::Colours::black.withAlpha (0.32f * ease));

    const auto panel = findColour (ThemeColourIds::panel);
    const auto border = findColour (ThemeColourIds::border);
    const auto text = findColour (juce::Label::textColourId);
    const auto muted = findColour (ThemeColourIds::textMuted);
    const auto accent = findColour (ThemeColourIds::accent);
    const auto step = juce::MathConstants<float>::twoPi / (float) items.size();

    g.saveState();
    g.setOpacity (ease);
    const auto scale = 0.85f + 0.15f * ease;   // 開くとき: 85% の大きさから広がる
    g.addTransform (juce::AffineTransform::scale (scale, scale, centre.x, centre.y));

    // 円の影 (カードが浮いて見えるように)
    {
        juce::Path disc;
        disc.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 28, { 0, 8 }).drawForPath (g, disc);
    }

    // 扇形: 選んでいないものを先に、選んでいるもの (外に伸びる) を最後に描く
    std::vector<int> order;
    for (int i = 0; i < (int) items.size(); ++i)
        if (i != hovered)
            order.push_back (i);
    if (hovered >= 0)
        order.push_back (hovered);

    const auto drawRing = [&]
    {
        // 外周の線。選んで伸びた扇形の上には引かないよう、その扇形より先に描く
        g.setColour (border);
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);
    };

    for (auto i : order)
    {
        if (i == hovered)
            drawRing();

        const auto amount = hoverAmounts[(size_t) i];
        const bool enabled = items[(size_t) i].enabled;
        const auto r = radius * (1.0f + hoverGrow * amount);
        const auto sector = makeSector (i, r);

        // 使えない操作は、選んでもアクセントにしない (押せないボタンと同じく、薄いまま)
        const auto highlight = enabled ? amount : amount * 0.25f;
        g.setColour (panel.interpolatedWith (enabled ? accent : muted, highlight));
        g.fillPath (sector);

        g.setColour (border);
        g.strokePath (sector, juce::PathStrokeType (1.0f));

        // 文字: 扇形の真ん中あたり。選ぶと大きく・太くなる
        const auto angle = (float) i * step;
        const auto at = centre.getPointOnCircumference (radius * 0.62f + radius * 0.05f * amount, angle);
        const auto fontSize = 22.0f + 6.0f * amount;
        // 選んでいる操作の名前は白 (作者の指示。アクセントの色の扇形の上で読みやすく)。選ばれ具合に合わせて、なめらかに変わる
        const auto colour = ! enabled ? muted : text.interpolatedWith (juce::Colours::white, amount);
        g.setColour (colour);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (fontSize, amount > 0.5f && enabled ? juce::Font::bold : juce::Font::plain)));
        g.drawFittedText (items[(size_t) i].label,
                          juce::Rectangle<float> (radius * 0.68f, fontSize * 2.7f).withCentre (at).toNearestInt(),
                          juce::Justification::centred, 2, 0.8f);
    }

    if (hovered < 0)
        drawRing();

    // 中心: ここで離すと何もしない。背景の色の丸
    const auto dead = radius * deadZone;
    const auto hub = juce::Rectangle<float> (dead * 2.0f, dead * 2.0f).withCentre (centre);
    g.setColour (findColour (juce::ResizableWindow::backgroundColourId));
    g.fillEllipse (hub);
    g.setColour (border);
    g.drawEllipse (hub, 1.0f);
    g.setColour (muted);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawFittedText (hovered < 0 ? "離すと閉じる"_jp : juce::String(),
                      hub.reduced (6.0f).toNearestInt(), juce::Justification::centred, 2);

    g.restoreState();
}
