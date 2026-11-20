#include "PluginToneCard.h"
#include "../Audio/PluginLocator.h"
#include "../Platform/WindowOwner.h"
#include "../Theme/DeathMetalLookAndFeel.h"
#include "../AppLog.h"
#include "../Text.h"

// プラグインの編集画面を入れるウィンドウ。
// 閉じるボタンでは、編集画面ごと消す (カードの「別ウィンドウで開く」で作り直す)。
// 隠すだけにすると、プラグインの画面が見えない窓にぶら下がったまま残る。DAW も閉じたら消すのがふつうで、
// 隠したままにしていると Archetype Misha Mansoor X が落ちることがあった
class PluginToneCard::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& name, juce::AudioProcessorEditor& editor, std::function<void()> onCloseRequested)
        : DocumentWindow (name, juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);

        // 編集画面はカードが持つ (ウィンドウを先に消す)。ウィンドウの大きさは、編集画面に合わせる
        setContentNonOwned (&editor, true);

        // 大きさを変えられるプラグインなら、ウィンドウの端を引っ張って変えられる (どこまで小さくできるかはプラグインが決める)
        setResizable (editor.isResizable(), false);
        if (editor.isResizable())
            setResizeLimits (200, 150, 8000, 6000);
    }

    // このウィンドウの処理の途中で自分を消さないよう、消すのは次のメッセージで (カードが行う)
    void closeButtonPressed() override
    {
        if (onClose != nullptr)
            onClose();
    }

private:
    std::function<void()> onClose;
};

//==============================================================================
// 左の大きな枠 (タブ譜の場所) に、プラグインの編集画面を出す部品 (作者の指示)。
// 上の帯にプラグインの名前と「別ウィンドウで開く」「閉じる」、その下の真ん中に編集画面。
// 編集画面が枠より大きいときは縮める (大きさを変えられるプラグインは大きさを、変えられないものは表示の倍率を)。
// それでも入らないとき (縮められないプラグイン) は、onDoesNotFit で別のウィンドウに移してもらう
// (VST3 の画面は Windows の窓なので、枠からはみ出すと右のパネルの上に重なってしまうため)
class PluginToneCard::MainView final : public juce::Component,
                                       private juce::ComponentListener,
                                       private juce::Timer
{
public:
    MainView()
    {
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        windowButton.setButtonText ("別ウィンドウで開く"_jp);
        closeButton.setButtonText ("閉じる"_jp);
        windowButton.onClick = [this] { if (onToWindow != nullptr) onToWindow(); };
        closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };

        for (auto* c : std::initializer_list<juce::Component*> { &title, &windowButton, &closeButton })
        {
            addAndMakeVisible (c);
            c->setMouseClickGrabsKeyboardFocus (false);
            c->setWantsKeyboardFocus (false);
        }
    }

    ~MainView() override { setEditor (nullptr); }

    std::function<void()> onClose, onToWindow, onDoesNotFit;

    void setTitle (const juce::String& name) { title.setText (name, juce::dontSendNotification); }

    // 作った編集画面を受け取る (子にするのはカード。ここでは大きさと位置を合わせる)
    void setEditor (juce::AudioProcessorEditor* newEditor)
    {
        if (editor != nullptr)
            editor->removeComponentListener (this);

        editor = newEditor;
        scale = 1.0f;
        stopTimer();

        if (editor != nullptr)
        {
            natural = editor->getLocalBounds();
            editor->addComponentListener (this);
            fit();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (findColour (ThemeColourIds::panel));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (bounds, 10.0f, 1.0f);
        g.fillRect (getLocalBounds().reduced (1, 0).removeFromTop (barHeight).removeFromBottom (1));
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (12, 6);
        closeButton.setBounds (bar.removeFromRight (80));
        bar.removeFromRight (8);
        windowButton.setBounds (bar.removeFromRight (150));
        bar.removeFromRight (8);
        title.setBounds (bar);
        fit();
    }

    void visibilityChanged() override
    {
        // 隠している間 (チューナーなどを重ねている) に「入らない」と判断しない
        if (! isVisible())
            stopTimer();
    }

private:
    static constexpr int barHeight = 44;

    juce::Rectangle<int> getEditorArea() const { return getLocalBounds().withTrimmedTop (barHeight).reduced (8); }

    void fit()
    {
        const auto area = getEditorArea();
        if (editor == nullptr || area.isEmpty() || natural.isEmpty())
            return;

        fitting = true;
        const bool tooBig = natural.getWidth() > area.getWidth() || natural.getHeight() > area.getHeight();

        if (editor->isResizable())
        {
            // 大きさを変えられるプラグイン: 枠に入る大きさにする (元より大きくはしない。どこまで縮むかはプラグインが決める)
            const auto w = juce::jmin (natural.getWidth(), area.getWidth());
            const auto h = juce::jmin (natural.getHeight(), area.getHeight());
            if (w != editor->getWidth() || h != editor->getHeight())
                editor->setSize (w, h);
        }
        else if (tooBig || ! juce::approximatelyEqual (scale, 1.0f))
        {
            // 変えられないプラグイン: 表示の倍率で縮める (効くかはプラグイン次第。Misha は半分でも描けた)
            const auto newScale = tooBig ? juce::jmin ((float) area.getWidth() / (float) natural.getWidth(),
                                                       (float) area.getHeight() / (float) natural.getHeight())
                                         : 1.0f;
            if (! juce::approximatelyEqual (newScale, scale))
            {
                scale = newScale;
                editor->setScaleFactor (scale);
            }
        }

        fitting = false;
        centre();
    }

    void centre()
    {
        if (editor == nullptr)
            return;

        const auto area = getEditorArea();
        const auto shown = editor->getBoundsInParent();   // 倍率を掛けた後の、見えている大きさ
        auto target = shown.withCentre (area.getCentre());
        if (shown.getWidth() > area.getWidth())   target.setX (area.getX());
        if (shown.getHeight() > area.getHeight()) target.setY (area.getY());

        // 倍率を JUCE の変換で掛けている編集画面 (つまみを並べただけの汎用の画面など) は、位置も倍率の前の座標で置く
        const auto s = juce::jmax (0.01f, editor->getTransform().mat00);   // 縮めるだけ (回さない) ので、横の倍率 = 倍率
        editor->setTopLeftPosition (juce::roundToInt ((float) target.getX() / s), juce::roundToInt ((float) target.getY() / s));

        // 入ったかは少し待ってから確かめる (プラグインは倍率を変えたあと、遅れて大きさを変えることがある)
        fits = shown.getWidth() <= area.getWidth() + 1 && shown.getHeight() <= area.getHeight() + 1;
        if (isShowing())
            startTimer (600);
    }

    void timerCallback() override
    {
        stopTimer();
        if (! fits && editor != nullptr && isShowing() && onDoesNotFit != nullptr)
            onDoesNotFit();
    }

    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        // プラグインが自分で大きさを変えた (倍率を変えた後や、プラグインの画面の中の大きさのボタン)
        if (wasResized && ! fitting)
            centre();
    }

    juce::Label title;
    juce::TextButton windowButton, closeButton;
    juce::AudioProcessorEditor* editor = nullptr;
    juce::Rectangle<int> natural;   // 開いたときの大きさ (倍率 1)
    float scale = 1.0f;
    bool fitting = false, fits = true;
};

//==============================================================================
PluginToneCard::PluginToneCard (AudioEngine& e)
    : engine (e)
{
    titleLabel.setFont (juce::FontOptions (15.0f));
    messageLabel.setJustificationType (juce::Justification::topLeft);
    messageLabel.setMinimumHorizontalScale (1.0f);
    messageLabel.setFont (juce::FontOptions (12.0f));

    downloadButton.setButtonText ("TONE3000 (無料) のページを開く"_jp);
    downloadButton.setTooltip (juce::String (PluginLocator::tone3000DownloadUrl));
    downloadButton.onClick = [] { juce::URL (PluginLocator::tone3000DownloadUrl).launchInDefaultBrowser(); };

    chooseButton.setButtonText ("VSTを選択"_jp);
    chooseButton.setTooltip ("今のプリセットで使う VST3 プラグイン (.vst3) を選びます"_jp);
    chooseButton.onClick = [this] { if (onChooseFile != nullptr) onChooseFile(); };

    rescanButton.setButtonText ("探し直す"_jp);
    rescanButton.setTooltip ("インストールしたあとに押すと、もう一度探します"_jp);
    rescanButton.onClick = [this] { if (onRescan != nullptr) onRescan(); };

    // 「プラグインを開く」: 左の大きな枠 (タブ譜の場所) に出す (作者の指示)。出しているときは「プラグインを閉じる」
    openMainButton.setButtonText ("プラグインを開く"_jp);
    openMainButton.setTooltip ("プラグインの画面を、左の大きな枠 (タブ譜の場所) に出します"_jp);
    openMainButton.onClick = [this]
    {
        if (place == Place::main)
            closeEditor();
        else
            openAt (Place::main);
    };

    openWindowButton.setButtonText ("別ウィンドウで開く"_jp);
    openWindowButton.setTooltip ("プラグインの画面を、別のウィンドウで開きます"_jp);
    openWindowButton.onClick = [this] { openAt (Place::window); };

    // 3つ並ぶボタンは幅が狭いので、文字を小さめにする (作者のレイアウト)
    for (auto* b : { &chooseButton, &openMainButton, &openWindowButton })
        b->getProperties().set ("fontHeight", 12.5f);

    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &messageLabel, &downloadButton, &chooseButton,
                                                            &rescanButton, &openMainButton, &openWindowButton })
    {
        addChildComponent (c);
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }
    titleLabel.setVisible (true);

    mainView = std::make_unique<MainView>();
    mainView->onClose = [this]
    {
        // 「閉じる」のボタンの処理の途中で、そのボタンの親を触らないよう、次のメッセージで
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)] { if (safe != nullptr) safe->closeEditor(); });
    };
    mainView->onToWindow = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)] { if (safe != nullptr) safe->openAt (Place::window); });
    };
    mainView->onDoesNotFit = [this]
    {
        if (place != Place::main)
            return;
        openAt (Place::window);
        if (onStatus != nullptr)
            onStatus ("プラグインの画面が枠に入らないので、別のウィンドウで開きました"_jp);
    };

    setState (State::notFound);
}

PluginToneCard::~PluginToneCard()
{
    deleteEditor();
}

juce::Component& PluginToneCard::getMainView() noexcept
{
    return *mainView;
}

void PluginToneCard::setState (State newState, const juce::String& message)
{
    state = newState;

    switch (state)
    {
        case State::notFound:
            messageLabel.setText (message.isNotEmpty() ? message
                                  : "「VSTを選択」でプラグインを選んでください。\n"
                                    "無料の TONE3000 なら、入れてから「探し直す」を押します。"_jp,
                                  juce::dontSendNotification);
            break;
        case State::loading:
            messageLabel.setText ("プラグインを読み込んでいます…"_jp, juce::dontSendNotification);
            break;
        case State::error:
            messageLabel.setText (message, juce::dontSendNotification);
            break;
        case State::ready:
            messageLabel.setText ({}, juce::dontSendNotification);
            break;
    }

    updateControls();
}

void PluginToneCard::updateControls()
{
    titleLabel.setText (state == State::ready && pluginName.isNotEmpty() ? pluginName : "VST プラグイン"_jp,
                        juce::dontSendNotification);
    titleLabel.setTooltip (titleLabel.getText());
    mainView->setTitle (pluginName);

    const bool needsHelp = state == State::notFound || state == State::error;
    const bool ready = state == State::ready;

    messageLabel.setVisible (! ready);
    downloadButton.setVisible (needsHelp);
    rescanButton.setVisible (needsHelp);
    chooseButton.setVisible (true);
    openMainButton.setVisible (true);
    openWindowButton.setVisible (true);
    openMainButton.setEnabled (ready);
    openWindowButton.setEnabled (ready);
    openMainButton.setButtonText (place == Place::main ? "プラグインを閉じる"_jp : "プラグインを開く"_jp);
    openMainButton.setToggleState (place == Place::main, juce::dontSendNotification);

    resized();
    if (onSizeChanged != nullptr)
        onSizeChanged();
}

//==============================================================================
// エンジンのプラグインが変わった (または、この画面が受け取りを始めた)。
// 編集画面は、開くときにだけ作る (見えない編集画面を持ち続けない)
void PluginToneCard::refreshEditor()
{
    const auto wasOpen = place != Place::none ? place : reopenPlace;
    deleteEditor();

    auto* tone = engine.getPluginTone();
    if (tone == nullptr)
    {
        // 差し替えの途中 (古いプラグインを外した)。開いていたなら、新しいプラグインができたときに開き直す
        reopenPlace = wasOpen;
        return;
    }

    pluginName = tone->getName();
    setState (State::ready);

    reopenPlace = Place::none;
    if (wasOpen != Place::none)
        openAt (wasOpen);
}

bool PluginToneCard::createEditor (juce::Component* parent)
{
    auto* tone = engine.getPluginTone();
    if (tone == nullptr)
        return false;

    auto& instance = tone->getInstance();

    // ほかの場所 (消える途中の前の画面など) がまだ編集画面を持っていたら、作らない (二重に持つと、二重に消してしまう)
    if (instance.getActiveEditor() != nullptr)
        return false;

    // 編集画面がないプラグインは、つまみを並べただけの汎用の画面にする
    // (createEditorAndMakeActive は、すでに編集画面があれば nullptr を返す)
    if (instance.hasEditor())
        editor.reset (instance.createEditorAndMakeActive());
    if (editor == nullptr)
        editor = std::make_unique<juce::GenericAudioProcessorEditor> (instance);

    // 編集画面は、最初から出す場所の中に作る (途中で親を変えると表示されないことがあるため)
    if (parent != nullptr)
        parent->addAndMakeVisible (*editor);
    else
        window = std::make_unique<PluginWindow> (pluginName, *editor, [this] { closeWindow(); });

    return true;
}

bool PluginToneCard::createEditorWindow()
{
    return createEditor (nullptr);
}

void PluginToneCard::closeWindow()
{
    // ウィンドウの閉じるボタンの処理の途中で、そのウィンドウを消さないよう、次のメッセージで消す
    AppLog::write ("プラグインのウィンドウを閉じる"_jp);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe == nullptr)
            return;

        safe->closeEditor();

        // 手前に出ていたウィンドウを消すと、Windows はほかのアプリのウィンドウを前に出すことがある
        // (アプリが閉じたように見える)。アプリのウィンドウを前に出しておく
        if (auto* top = safe->getTopLevelComponent(); top != nullptr && top->isOnDesktop())
            top->toFront (true);

        AppLog::write ("プラグインのウィンドウを閉じた"_jp);
    });
}

void PluginToneCard::deleteEditor()
{
    // 編集画面を先に消す。編集画面のデストラクタが、プラグインに「画面を外す」(VST3 の removed) を知らせる。
    // ウィンドウを先に消すと、プラグインの窓 (Windows の HWND) が、プラグインが知らないうちに壊され、
    // その後に届いたメッセージでプラグインが落ちる (Archetype Misha Mansoor X のクラッシュダンプで確認)
    if (place != Place::none)
        reopenPlace = place;   // 差し替えのあと (refreshEditor) に、同じ場所で開き直す

    if (mainView != nullptr)
        mainView->setEditor (nullptr);
    editor = nullptr;   // ウィンドウ・左の枠からも自動で外れる
    window = nullptr;
    setPlace (Place::none);
}

void PluginToneCard::closeEditor()
{
    deleteEditor();
    reopenPlace = Place::none;
}

void PluginToneCard::requestOpen (Place where)
{
    if (state == State::ready && engine.getPluginTone() != nullptr && ! engine.isPluginToneLoading())
        openAt (where);
    else
        reopenPlace = where;   // 読み込み中: できたときに開く
}

void PluginToneCard::openAt (Place where)
{
    reopenPlace = Place::none;
    if (where == Place::none)
        return;

    // 別の場所に出していたら、消してから作り直す (編集画面は同時に1つだけ。親は変えない)
    if (place != where && editor != nullptr)
    {
        deleteEditor();
        reopenPlace = Place::none;
    }

    if (where == Place::main)
    {
        if (editor == nullptr)
        {
            // 左の枠を先に出してから (練習画面が onPlaceChanged で枠を置く)、その中に作る
            setPlace (Place::main);
            if (! createEditor (mainView.get()))
            {
                setPlace (Place::none);
                return;
            }
            mainView->setEditor (editor.get());
        }
        return;
    }

    // 別のウィンドウ
    if (window == nullptr && ! createEditorWindow())
        return;

    setPlace (Place::window);

    // アプリのウィンドウを持ち主にして、その手前に出す (アプリをクリックしても後ろに隠れない)
    if (! window->isOnDesktop())
        window->addToDesktop();

    if (auto* top = getTopLevelComponent(); top != nullptr && top->isOnDesktop())
        WindowOwner::setOwner (window->getWindowHandle(), top->getWindowHandle());

    // 初めて開くときは、アプリのウィンドウの中央に置く。
    // ただし、画面 (モニターの作業領域) からはみ出さないようにする。
    // プラグインの画面はアプリより大きいことがあり (Archetype Misha Mansoor X は 1131 × 738)、
    // 中央に合わせるだけだと左上が画面の外に出て、ウィンドウが見えなくなる
    if (! window->isVisible())
    {
        if (auto* top = getTopLevelComponent())
        {
            auto bounds = window->getBounds().withCentre (top->getScreenBounds().getCentre());

            if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
                bounds = bounds.constrainedWithin (display->userBounds.toNearestInt());

            window->setTopLeftPosition (bounds.getPosition());
        }
    }

    window->setVisible (true);
    window->toFront (true);
}

void PluginToneCard::setPlace (Place newPlace)
{
    if (place == newPlace)
        return;

    place = newPlace;
    openMainButton.setButtonText (place == Place::main ? "プラグインを閉じる"_jp : "プラグインを開く"_jp);
    openMainButton.setToggleState (place == Place::main, juce::dontSendNotification);

    if (onPlaceChanged != nullptr)
        onPlaceChanged();
}

void PluginToneCard::visibilityChanged()
{
    // 音色を VST から変えたら (カードが隠れたら)、プラグインの画面を閉じる (編集画面ごと消す)
    if (! isVisible())
        closeEditor();
}

bool PluginToneCard::isWindowShowing() const
{
    return window != nullptr && window->isShowing();
}

juce::Rectangle<int> PluginToneCard::getWindowScreenBounds() const
{
    return window != nullptr ? window->getScreenBounds() : juce::Rectangle<int>();
}

juce::Rectangle<int> PluginToneCard::getEditorSize() const
{
    return editor != nullptr ? editor->getLocalBounds() : juce::Rectangle<int>();
}

void* PluginToneCard::getWindowNativeHandle() const
{
    return window != nullptr && window->isOnDesktop() ? window->getWindowHandle() : nullptr;
}

//==============================================================================
// 作者のレイアウト: 1行目にプラグインの名前、2行目に「プラグインを開く」「VSTを選択」「別ウィンドウで開く」。
// 見つからない・読み込めないときは、名前の下に案内の文と「TONE3000 のページ」「探し直す」
int PluginToneCard::getPreferredHeight() const
{
    const bool needsHelp = state == State::notFound || state == State::error;
    const bool loading = state == State::loading;
    return headerHeight + (needsHelp ? 4 + messageHeight + 6 + buttonHeight + 6 + buttonHeight : 0)
                        + (loading ? 4 + 20 : 0)
                        + 6 + buttonHeight;
}

void PluginToneCard::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds (area.removeFromTop (headerHeight));

    const bool needsHelp = state == State::notFound || state == State::error;
    if (needsHelp)
    {
        area.removeFromTop (4);
        messageLabel.setBounds (area.removeFromTop (messageHeight));
        area.removeFromTop (6);
        downloadButton.setBounds (area.removeFromTop (buttonHeight));
        area.removeFromTop (6);
        rescanButton.setBounds (area.removeFromTop (buttonHeight));
    }
    else if (state == State::loading)
    {
        area.removeFromTop (4);
        messageLabel.setBounds (area.removeFromTop (20));
    }

    // 「プラグインを開く」「VSTを選択」「別ウィンドウで開く」を同じ幅で (作者のレイアウト)
    area.removeFromTop (6);
    auto row = area.removeFromTop (buttonHeight);
    constexpr int gap = 8;
    const auto w = (row.getWidth() - 2 * gap) / 3;
    openMainButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    chooseButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    openWindowButton.setBounds (row);
}
