#include "PluginToneCard.h"
#include "../Audio/PluginLocator.h"
#include "../Platform/WindowOwner.h"
#include "../Text.h"

// プラグインの編集画面を入れるウィンドウ。閉じるボタンでは隠すだけ (カードの「プラグインの画面を開く」で、また出す)
class PluginToneCard::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& name, juce::AudioProcessorEditor& editor)
        : DocumentWindow (name, juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);

        // 編集画面はカードが持つ (ウィンドウを先に消す)。ウィンドウの大きさは、編集画面に合わせる
        setContentNonOwned (&editor, true);

        // 大きさを変えられるプラグインなら、ウィンドウの端を引っ張って変えられる (どこまで小さくできるかはプラグインが決める)
        setResizable (editor.isResizable(), false);
        if (editor.isResizable())
            setResizeLimits (200, 150, 8000, 6000);
    }

    void closeButtonPressed() override { setVisible (false); }
};

//==============================================================================
PluginToneCard::PluginToneCard (AudioEngine& e)
    : engine (e)
{
    titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    messageLabel.setJustificationType (juce::Justification::topLeft);
    messageLabel.setMinimumHorizontalScale (1.0f);

    downloadButton.setButtonText ("ダウンロードページを開く"_jp);
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
                                  : "TONE3000 プラグインが見つかりません。\n"
                                    "公式サイトから無料でダウンロードしてインストールし、「探し直す」を押してください。"_jp,
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
    titleLabel.setText (state == State::ready && pluginName.isNotEmpty() ? pluginName : juce::String ("TONE3000"),
                        juce::dontSendNotification);

    const bool needsHelp = state == State::notFound || state == State::error;
    const bool ready = state == State::ready && editor != nullptr;

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
void PluginToneCard::refreshEditor()
{
    deleteEditor();

    auto* tone = engine.getPluginTone();
    if (tone == nullptr)
        return;

    auto& instance = tone->getInstance();

    // ほかの場所 (消える途中の前の画面など) がまだ編集画面を持っていたら、作らない (二重に持つと、二重に消してしまう)
    if (instance.getActiveEditor() != nullptr)
        return;

    // 編集画面がないプラグインは、つまみを並べただけの汎用の画面にする
    // (createEditorAndMakeActive は、すでに編集画面があれば nullptr を返す)
    if (instance.hasEditor())
        editor.reset (instance.createEditorAndMakeActive());
    if (editor == nullptr)
        editor = std::make_unique<juce::GenericAudioProcessorEditor> (instance);

    pluginName = tone->getName();

    // 編集画面は、最初から別のウィンドウの中に作る (途中で親を変えると表示されないことがあるため)
    window = std::make_unique<PluginWindow> (pluginName, *editor);

    setState (State::ready);

    if (openWhenReady)
        openWindow();
}

void PluginToneCard::deleteEditor()
{
    window = nullptr;   // ウィンドウを先に消す (中身として編集画面を指しているため)
    editor = nullptr;   // プラグインより先に消す
}

void PluginToneCard::requestOpenWindow()
{
    if (window != nullptr)
        openWindow();
    else
        openWhenReady = true;   // 読み込み中・まだ作っていない: できたときに開く
}

void PluginToneCard::openWindow()
{
    openWhenReady = false;

    if (window == nullptr)
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
    // 音色を TONE3000 から変えたら (カードが隠れたら)、プラグインのウィンドウも隠す
    if (! isVisible() && window != nullptr)
        window->setVisible (false);
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
    const bool ready = state == State::ready && editor != nullptr;
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
