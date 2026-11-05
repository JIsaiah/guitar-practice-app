#include "PluginToneCard.h"
#include "../Audio/PluginLocator.h"
#include "../Platform/WindowOwner.h"
#include "../AppLog.h"
#include "../Text.h"

// プラグインの編集画面を入れるウィンドウ。
// 閉じるボタンでは、編集画面ごと消す (カードの「プラグインの画面を開く」で作り直す)。
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
PluginToneCard::PluginToneCard (AudioEngine& e)
    : engine (e)
{
    titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    messageLabel.setJustificationType (juce::Justification::topLeft);
    messageLabel.setMinimumHorizontalScale (1.0f);

    downloadButton.setButtonText ("TONE3000 (無料) のページを開く"_jp);
    downloadButton.setTooltip (juce::String (PluginLocator::tone3000DownloadUrl));
    downloadButton.onClick = [] { juce::URL (PluginLocator::tone3000DownloadUrl).launchInDefaultBrowser(); };

    chooseButton.setButtonText ("VST3 を選ぶ…"_jp);
    chooseButton.setTooltip ("インストールした場所が標準と違うときや、ほかのプラグインを使うときに、.vst3 を直接選びます"_jp);
    chooseButton.onClick = [this] { if (onChooseFile != nullptr) onChooseFile(); };

    rescanButton.setButtonText ("探し直す"_jp);
    rescanButton.setTooltip ("インストールしたあとに押すと、もう一度探します"_jp);
    rescanButton.onClick = [this] { if (onRescan != nullptr) onRescan(); };

    openWindowButton.setButtonText ("プラグインの画面を開く"_jp);
    openWindowButton.onClick = [this] { openWindow(); };

    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &messageLabel,
                                                            &downloadButton, &chooseButton, &rescanButton, &openWindowButton })
    {
        addChildComponent (c);
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }
    titleLabel.setVisible (true);
    messageLabel.setVisible (true);

    setState (State::notFound);
}

PluginToneCard::~PluginToneCard()
{
    deleteEditor();
}

void PluginToneCard::setState (State newState, const juce::String& message)
{
    state = newState;

    switch (state)
    {
        case State::notFound:
            messageLabel.setText (message.isNotEmpty() ? message
                                  : "「VST3 を選ぶ…」でプラグインを選んでください。\n"
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
            messageLabel.setText ("プラグインの画面は、別のウィンドウで開きます。閉じても、下のボタンでまた開けます。"_jp,
                                  juce::dontSendNotification);
            break;
    }

    updateControls();
}

void PluginToneCard::updateControls()
{
    titleLabel.setText (state == State::ready && pluginName.isNotEmpty() ? pluginName : "VST プラグイン"_jp,
                        juce::dontSendNotification);

    const bool needsHelp = state == State::notFound || state == State::error;
    const bool ready = state == State::ready;

    downloadButton.setVisible (needsHelp);
    rescanButton.setVisible (needsHelp);
    chooseButton.setVisible (needsHelp || state == State::ready);
    chooseButton.setButtonText (state == State::ready ? "選び直す…"_jp : "VST3 を選ぶ…"_jp);
    openWindowButton.setVisible (ready);

    resized();
    if (onSizeChanged != nullptr)
        onSizeChanged();
}

//==============================================================================
// エンジンのプラグインが変わった (または、この画面が受け取りを始めた)。
// 編集画面は、ウィンドウを開くときにだけ作る (見えない編集画面を持ち続けない)
void PluginToneCard::refreshEditor()
{
    const bool wasOpen = window != nullptr && window->isVisible();
    deleteEditor();

    auto* tone = engine.getPluginTone();
    if (tone == nullptr)
    {
        // 差し替えの途中 (古いプラグインを外した)。開いていたなら、新しいプラグインができたときに開き直す
        if (wasOpen)
            openWhenReady = true;
        return;
    }

    pluginName = tone->getName();
    setState (State::ready);

    if (openWhenReady || wasOpen)
        openWindow();
}

bool PluginToneCard::createEditorWindow()
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

    // 編集画面は、最初から別のウィンドウの中に作る (途中で親を変えると表示されないことがあるため)
    window = std::make_unique<PluginWindow> (pluginName, *editor, [this] { closeWindow(); });
    return true;
}

void PluginToneCard::closeWindow()
{
    // ウィンドウの閉じるボタンの処理の途中で、そのウィンドウを消さないよう、次のメッセージで消す
    AppLog::write ("プラグインのウィンドウを閉じる");

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe == nullptr)
            return;

        safe->deleteEditor();

        // 手前に出ていたウィンドウを消すと、Windows はほかのアプリのウィンドウを前に出すことがある
        // (アプリが閉じたように見える)。アプリのウィンドウを前に出しておく
        if (auto* top = safe->getTopLevelComponent(); top != nullptr && top->isOnDesktop())
            top->toFront (true);

        AppLog::write ("プラグインのウィンドウを閉じた");
    });
}

void PluginToneCard::deleteEditor()
{
    // 編集画面を先に消す。編集画面のデストラクタが、プラグインに「画面を外す」(VST3 の removed) を知らせる。
    // ウィンドウを先に消すと、プラグインの窓 (Windows の HWND) が、プラグインが知らないうちに壊され、
    // その後に届いたメッセージでプラグインが落ちる (Archetype Misha Mansoor X のクラッシュダンプで確認)
    editor = nullptr;   // ウィンドウからも自動で外れる (ウィンドウは中身を SafePointer で持つ)
    window = nullptr;
}

void PluginToneCard::requestOpenWindow()
{
    if (state == State::ready && engine.getPluginTone() != nullptr && ! engine.isPluginToneLoading())
        openWindow();
    else
        openWhenReady = true;   // 読み込み中: できたときに開く
}

void PluginToneCard::openWindow()
{
    openWhenReady = false;

    if (window == nullptr && ! createEditorWindow())
        return;

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

void PluginToneCard::visibilityChanged()
{
    // 音色を TONE3000 から変えたら (カードが隠れたら)、プラグインのウィンドウを閉じる (編集画面ごと消す)
    if (! isVisible())
        deleteEditor();
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
int PluginToneCard::getPreferredHeight() const
{
    const bool needsHelp = state == State::notFound || state == State::error;
    const bool ready = state == State::ready;
    return headerHeight + 4 + 44 + (needsHelp ? messageHeight - 44 + 8 + 30 + 6 + 30 : 0) + (ready ? 4 + 30 : 0);
}

void PluginToneCard::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight);
    if (state == State::ready)
        chooseButton.setBounds (header.removeFromRight (96).reduced (0, 1));
    titleLabel.setBounds (header);
    area.removeFromTop (4);

    const bool needsHelp = state == State::notFound || state == State::error;
    messageLabel.setBounds (area.removeFromTop (needsHelp ? messageHeight : 44));

    if (openWindowButton.isVisible())
    {
        area.removeFromTop (4);
        openWindowButton.setBounds (area.removeFromTop (30));
    }

    if (downloadButton.isVisible())
    {
        area.removeFromTop (8);
        downloadButton.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);
        auto row = area.removeFromTop (30);
        rescanButton.setBounds (row.removeFromRight (row.getWidth() / 2).withTrimmedLeft (3));
        chooseButton.setBounds (row.withTrimmedRight (3));
    }
}
