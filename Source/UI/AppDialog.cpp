#include "AppDialog.h"
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

namespace
{
    int openCount = 0;

    // 重ねる先のウィンドウ: near があれば、そのウィンドウ。無ければ、アプリのいちばん手前に出ているウィンドウ
    juce::Component* findWindowFor (juce::Component* near)
    {
        if (near != nullptr)
            if (auto* top = near->getTopLevelComponent())
                return top;

        if (auto* active = juce::TopLevelWindow::getActiveTopLevelWindow())
            return active;

        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
            if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i); w != nullptr && w->isShowing())
                return w;

        return nullptr;
    }
}

std::function<void()> AppOverlay::onOpenCountChanged;

int AppOverlay::getNumOpen() noexcept { return openCount; }

// 重ねたウィンドウの大きさが変わったら、いっぱいに合わせ直す
struct AppOverlay::ParentWatcher final : juce::ComponentListener
{
    ParentWatcher (AppOverlay& o, juce::Component& w) : owner (o), window (&w) { w.addComponentListener (this); }
    ~ParentWatcher() override { if (window != nullptr) window->removeComponentListener (this); }

    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (wasResized)
            owner.setBounds (c.getLocalBounds());
    }

    AppOverlay& owner;
    juce::Component::SafePointer<juce::Component> window;
};

AppOverlay::AppOverlay()
{
    setWantsKeyboardFocus (true);
}

AppOverlay::~AppOverlay()
{
    parentWatcher = nullptr;
    if (counted)
    {
        --openCount;
        if (onOpenCountChanged != nullptr)
            onOpenCountChanged();   // 練習画面: 隠していたタブ譜・動画・プラグインの画面を出す
    }
}

void AppOverlay::showOver (juce::Component* near, juce::ModalComponentManager::Callback* callback, bool deleteWhenDismissed)
{
    auto* window = findWindowFor (near);
    if (window == nullptr)
    {
        // ウィンドウが無い (起動の途中など): 何も出さずに、閉じたことにする
        if (callback != nullptr)
        {
            callback->modalStateFinished (0);
            delete callback;
        }
        if (deleteWhenDismissed)
            delete this;
        return;
    }

    window->addAndMakeVisible (this);
    setBounds (window->getLocalBounds());
    toFront (false);
    parentWatcher = std::make_unique<ParentWatcher> (*this, *window);

    if (! counted)
    {
        counted = true;
        ++openCount;
        if (onOpenCountChanged != nullptr)
            onOpenCountChanged();   // 練習画面: タブ譜・動画・プラグインの画面 (別の窓) を隠す
    }

    Component::enterModalState (true, callback, deleteWhenDismissed);
}

void AppOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f));
    const auto box = getBox().toFloat();
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (box, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);
}

void AppOverlay::resized()
{
    layoutBox (getBox());
}

//==============================================================================
AppDialog::AppDialog (const juce::String& title, const juce::String& message, juce::MessageBoxIconType, juce::Component* associatedComponent)
    : titleText (title), messageText (message), associated (associatedComponent)
{
    errorLabel.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f, juce::Font::bold)));
    errorLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (errorLabel);
}

void AppDialog::addTextEditor (const juce::String& name, const juce::String& initialContents, const juce::String& label, bool isPasswordBox)
{
    Field f;
    f.name = name;
    if (label.isNotEmpty())
    {
        f.label = std::make_unique<juce::Label> (juce::String(), label);
        f.label->setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
        addAndMakeVisible (*f.label);
    }
    f.editor = std::make_unique<juce::TextEditor> (name, isPasswordBox ? (juce::juce_wchar) 0x2022 : 0);
    f.editor->setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    f.editor->setText (initialContents, false);
    f.editor->onTextChange = [this] { errorLabel.setText ({}, juce::dontSendNotification); };

    // Enter・Esc は、そのキーのボタンと同じ (入力欄がキーを受け取るため、ここで渡す)
    f.editor->onReturnKey = [this] { keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); };
    f.editor->onEscapeKey = [this] { keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); };
    addAndMakeVisible (*f.editor);
    fields.push_back (std::move (f));
}

void AppDialog::addComboBox (const juce::String& name, const juce::StringArray& items, const juce::String& label)
{
    Field f;
    f.name = name;
    if (label.isNotEmpty())
    {
        f.label = std::make_unique<juce::Label> (juce::String(), label);
        f.label->setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
        addAndMakeVisible (*f.label);
    }
    f.combo = std::make_unique<juce::ComboBox> (name);
    f.combo->addItemList (items, 1);
    f.combo->setSelectedItemIndex (0, juce::dontSendNotification);
    addAndMakeVisible (*f.combo);
    fields.push_back (std::move (f));
}

void AppDialog::addButton (const juce::String& text, int returnValue, const juce::KeyPress& key, const juce::KeyPress& key2)
{
    ButtonInfo b { std::make_unique<juce::TextButton> (text), returnValue, key, key2 };
    b.button->onClick = [this, returnValue] { press (returnValue); };
    addAndMakeVisible (*b.button);
    buttons.push_back (std::move (b));
}

juce::TextEditor* AppDialog::getTextEditor (const juce::String& name) const
{
    for (const auto& f : fields)
        if (f.name == name && f.editor != nullptr)
            return f.editor.get();
    return nullptr;
}

juce::String AppDialog::getTextEditorContents (const juce::String& name) const
{
    if (auto* e = getTextEditor (name))
        return e->getText();
    return {};
}

juce::ComboBox* AppDialog::getComboBoxComponent (const juce::String& name) const
{
    for (const auto& f : fields)
        if (f.name == name && f.combo != nullptr)
            return f.combo.get();
    return nullptr;
}

void AppDialog::enterModalState (bool takeFocus, juce::ModalComponentManager::Callback* callback, bool deleteWhenDismissed)
{
    errorLabel.setColour (juce::Label::textColourId, findColour (ThemeColourIds::accent));
    auto* safeAssociated = associated.getComponent();
    showOver (safeAssociated, callback, deleteWhenDismissed);

    if (! takeFocus)
        return;

    // 入力欄があれば、最初の欄に文字を打てるようにする (中の文字は選んでおく)。無ければ、この画面がキーを受け取る
    for (auto& f : fields)
        if (f.editor != nullptr)
        {
            f.editor->grabKeyboardFocus();
            f.editor->selectAll();
            return;
        }
    grabKeyboardFocus();
}

void AppDialog::press (int returnValue)
{
    if (returnValue != 0 && validator != nullptr)
    {
        const auto error = validator (returnValue);
        if (error.isNotEmpty())
        {
            // 閉じずに、入力欄の下に知らせて、入れ直してもらう
            errorLabel.setText (error, juce::dontSendNotification);
            for (auto& f : fields)
                if (f.editor != nullptr)
                {
                    f.editor->grabKeyboardFocus();
                    f.editor->selectAll();
                    break;
                }
            return;
        }
    }

    exitModalState (returnValue);   // deleteWhenDismissed なら、このあと消える
}

bool AppDialog::keyPressed (const juce::KeyPress& key)
{
    for (const auto& b : buttons)
        if ((b.key.isValid() && b.key == key) || (b.key2.isValid() && b.key2 == key))
        {
            press (b.returnValue);
            return true;
        }

    // ボタンの決まりが無いときも、Esc は「閉じる」(いちばん後ろのボタン = キャンセル)
    if (key == juce::KeyPress::escapeKey && ! buttons.empty())
    {
        press (buttons.back().returnValue);
        return true;
    }
    return false;
}

void AppDialog::paint (juce::Graphics& g)
{
    AppOverlay::paint (g);

    auto area = getBox().reduced (padding);
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText (titleText, area.removeFromTop (28), juce::Justification::centredLeft, true);

    if (messageText.isNotEmpty())
    {
        area.removeFromTop (6);
        const auto layout = makeMessageLayout ((float) area.getWidth());
        layout.draw (g, area.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
    }
}

juce::TextLayout AppDialog::makeMessageLayout (float width) const
{
    juce::AttributedString text;
    text.append (messageText, juce::Font (AppLookAndFeel::scaled (juce::FontOptions (14.5f))), findColour (juce::Label::textColourId));
    text.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

int AppDialog::contentHeight() const
{
    int h = padding + 28;   // 題
    if (messageText.isNotEmpty())
        h += 6 + (int) std::ceil (makeMessageLayout ((float) (boxWidth - padding * 2)).getHeight());

    for (const auto& f : fields)
        h += 10 + (f.label != nullptr ? 20 : 0) + (f.editor != nullptr ? 34 : 30);

    h += validator != nullptr ? 26 : 8;   // 入力欄の下の知らせ
    h += 12 + 36 + padding;              // ボタン
    return h;
}

juce::Rectangle<int> AppDialog::getBox() const
{
    const auto width = juce::jmin (boxWidth, getWidth() - 40);
    const auto height = juce::jmin (contentHeight(), getHeight() - 40);
    return getLocalBounds().withSizeKeepingCentre (width, height);
}

void AppDialog::layoutBox (juce::Rectangle<int> box)
{
    auto area = box.reduced (padding);
    area.removeFromTop (28);
    if (messageText.isNotEmpty())
        area.removeFromTop (6 + (int) std::ceil (makeMessageLayout ((float) area.getWidth()).getHeight()));

    for (auto& f : fields)
    {
        area.removeFromTop (10);
        if (f.label != nullptr)
            f.label->setBounds (area.removeFromTop (20));
        if (f.editor != nullptr)
            f.editor->setBounds (area.removeFromTop (34));
        if (f.combo != nullptr)
            f.combo->setBounds (area.removeFromTop (30));
    }

    errorLabel.setBounds (area.removeFromTop (validator != nullptr ? 26 : 8).withTrimmedTop (4));

    // ボタンは右から、文字の幅に合わせて (最低 110)
    auto row = area.removeFromBottom (36);
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    {
        auto& b = *it->button;
        const auto width = juce::jmax (110, juce::GlyphArrangement::getStringWidthInt (juce::Font (AppLookAndFeel::scaled (juce::FontOptions (15.0f))),
                                                                                      b.getButtonText()) + 32);
        b.setBounds (row.removeFromRight (width));
        row.removeFromRight (8);
    }
}

//==============================================================================
void AppDialog::showAsync (const juce::MessageBoxOptions& options, std::function<void (int)> callback)
{
    auto* dialog = new AppDialog (options.getTitle(), options.getMessage(), options.getIconType(), options.getAssociatedComponent());

    // ボタンの番号は JUCE の AlertWindow と同じ (1つなら 0、2つなら 1・0、3つなら 1・2・0)
    const auto n = options.getNumButtons();
    for (int i = 0; i < n; ++i)
    {
        const auto value = (n == 1 || i == n - 1) ? 0 : i + 1;
        juce::KeyPress key;
        if (i == 0)
            key = juce::KeyPress (juce::KeyPress::returnKey);
        else if (i == n - 1)
            key = juce::KeyPress (juce::KeyPress::escapeKey);
        dialog->addButton (options.getButtonText (i), value, key);
    }
    if (n == 0)
        dialog->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey));

    dialog->enterModalState (true, callback != nullptr ? juce::ModalCallbackFunction::create (std::move (callback)) : nullptr, true);
}

void AppDialog::showMessageBoxAsync (juce::MessageBoxIconType iconType, const juce::String& title, const juce::String& message,
                                     const juce::String& buttonText, juce::Component* associatedComponent,
                                     juce::ModalComponentManager::Callback* callback)
{
    auto* dialog = new AppDialog (title, message, iconType, associatedComponent);
    dialog->addButton (buttonText.isNotEmpty() ? buttonText : juce::String ("OK"), 0,
                       juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, callback, true);
}

//==============================================================================
AppPanelOverlay::AppPanelOverlay (const juce::String& title, juce::Component* c, int width, int height)
    : titleText (title), content (c), contentWidth (width), contentHeight (height)
{
    closeButton.setButtonText ("閉じる"_jp);
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
    addAndMakeVisible (*content);
}

void AppPanelOverlay::close()
{
    // ボタンの処理の途中で自分を消さないよう、次のメッセージで閉じる (deleteWhenDismissed で消える)
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (this)]
    {
        if (safe != nullptr)
            safe->exitModalState (0);
    });
}

bool AppPanelOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void AppPanelOverlay::mouseDown (const juce::MouseEvent& e)
{
    // 後ろ (暗いところ) を押したら閉じる
    if (! getBox().contains (e.getPosition()))
        close();
}

void AppPanelOverlay::paint (juce::Graphics& g)
{
    AppOverlay::paint (g);
    auto bar = getBox().removeFromTop (titleHeight).reduced (20, 0);
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText (titleText, bar, juce::Justification::centredLeft, true);
    g.setColour (findColour (ThemeColourIds::border));
    g.fillRect (getBox().getX() + 1, getBox().getY() + titleHeight - 1, getBox().getWidth() - 2, 1);
}

juce::Rectangle<int> AppPanelOverlay::getBox() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (contentWidth, getWidth() - 40),
                                                   juce::jmin (contentHeight + titleHeight, getHeight() - 40));
}

void AppPanelOverlay::layoutBox (juce::Rectangle<int> box)
{
    auto bar = box.removeFromTop (titleHeight).reduced (12, 6);
    closeButton.setBounds (bar.removeFromRight (100));
    content->setBounds (box.reduced (1, 1));
}
