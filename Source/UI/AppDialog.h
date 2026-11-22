#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// アプリの中に重ねて出す画面の土台 (作者の指示: 別のウィンドウで開かない。アプリ全体をそうする)。
//
// JUCE の AlertWindow (確認・入力の画面) や DialogWindow (設定) は、Windows の窓を新しく作るので、出るまでに時間がかかる。
// この土台は、アプリのウィンドウの中に、ウィンドウいっぱいの部品として重ねる (後ろは薄く暗くし、押せないようにする)。
//
// 注意: タブ譜・動画のページ (WebView) とプラグインの画面は、Windows の別の窓として、アプリの部品の上に描かれる。
// 重ねた画面が隠れないよう、開いている間は練習画面がそれらを隠す (getNumOpen・onOpenCountChanged)
class AppOverlay : public juce::Component
{
public:
    AppOverlay();
    ~AppOverlay() override;

    // near があるウィンドウ (無ければ、アプリのいちばん手前のウィンドウ) に重ねて出し、モーダルにする
    void showOver (juce::Component* near, juce::ModalComponentManager::Callback* callback = nullptr, bool deleteWhenDismissed = true);

    // 今アプリの中に重ねている画面の数 / 変わったときの知らせ (練習画面が WebView を隠す・出す)
    static int getNumOpen() noexcept;
    static std::function<void()> onOpenCountChanged;

    void paint (juce::Graphics& g) override;   // 後ろを薄く暗くし、真ん中の箱 (getBox) を描く
    void resized() override;
    void parentHierarchyChanged() override { resized(); }

protected:
    virtual juce::Rectangle<int> getBox() const = 0;   // 真ん中の箱 (この部品の座標)
    virtual void layoutBox (juce::Rectangle<int> box) = 0;

private:
    struct ParentWatcher;
    std::unique_ptr<ParentWatcher> parentWatcher;   // ウィンドウの大きさが変わったら、いっぱいに合わせる
    bool counted = false;
};

//==============================================================================
// 確認・知らせ・文字の入力の画面。juce::AlertWindow と同じ使い方ができる (置き換えやすいように)
//   auto* d = new AppDialog ("題", "説明", juce::MessageBoxIconType::NoIcon, this);
//   d->addTextEditor ("name", "初めの文字");
//   d->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
//   d->addButton ("キャンセル", 0, juce::KeyPress (juce::KeyPress::escapeKey));
//   d->enterModalState (true, juce::ModalCallbackFunction::create (...), true);
class AppDialog final : public AppOverlay
{
public:
    AppDialog (const juce::String& title, const juce::String& message,
               juce::MessageBoxIconType iconType = juce::MessageBoxIconType::NoIcon, juce::Component* associatedComponent = nullptr);

    void addTextEditor (const juce::String& name, const juce::String& initialContents,
                        const juce::String& label = {}, bool isPasswordBox = false);
    void addComboBox (const juce::String& name, const juce::StringArray& items, const juce::String& label = {});
    void addButton (const juce::String& text, int returnValue, const juce::KeyPress& key = {}, const juce::KeyPress& key2 = {});

    juce::String getTextEditorContents (const juce::String& name) const;
    juce::TextEditor* getTextEditor (const juce::String& name) const;
    juce::ComboBox* getComboBoxComponent (const juce::String& name) const;

    // 0 以外のボタンを押したときに確かめる (エラーの文を返すと、閉じずに入力欄の下に出す。空なら閉じる)
    std::function<juce::String (int returnValue)> validator;

    // AlertWindow と同じ呼び方。アプリのウィンドウに重ねてから、モーダルにする
    void enterModalState (bool takeFocus, juce::ModalComponentManager::Callback* callback = nullptr, bool deleteWhenDismissed = false);

    // AlertWindow::showAsync・showMessageBoxAsync の代わり (ボタンの番号も同じ: 1つなら 0、2つなら 1・0、3つなら 1・2・0)
    static void showAsync (const juce::MessageBoxOptions& options, std::function<void (int)> callback);
    static void showMessageBoxAsync (juce::MessageBoxIconType iconType, const juce::String& title, const juce::String& message,
                                     const juce::String& buttonText = {}, juce::Component* associatedComponent = nullptr,
                                     juce::ModalComponentManager::Callback* callback = nullptr);

    bool keyPressed (const juce::KeyPress& key) override;
    void paint (juce::Graphics& g) override;

private:
    juce::Rectangle<int> getBox() const override;
    void layoutBox (juce::Rectangle<int> box) override;
    void press (int returnValue);
    int contentHeight() const;
    juce::TextLayout makeMessageLayout (float width) const;

    struct Field
    {
        juce::String name;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::TextEditor> editor;
        std::unique_ptr<juce::ComboBox> combo;
    };
    struct ButtonInfo
    {
        std::unique_ptr<juce::TextButton> button;
        int returnValue;
        juce::KeyPress key, key2;
    };

    juce::String titleText, messageText;
    juce::Component::SafePointer<juce::Component> associated;
    std::vector<Field> fields;
    std::vector<ButtonInfo> buttons;
    juce::Label errorLabel;

    static constexpr int boxWidth = 480, padding = 20;
};

//==============================================================================
// 設定・動作ログなど、中身のある画面を、アプリの中に重ねて出す (前は別のウィンドウ = DialogWindow)。
// 上に題と「閉じる」、その下に中身。Esc・「閉じる」・後ろ (暗いところ) のクリックで閉じる
class AppPanelOverlay final : public AppOverlay
{
public:
    // content は持ち主になる。width・height は中身の大きさ (ウィンドウに入らなければ縮める)
    AppPanelOverlay (const juce::String& title, juce::Component* content, int width, int height);

    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void paint (juce::Graphics& g) override;

private:
    juce::Rectangle<int> getBox() const override;
    void layoutBox (juce::Rectangle<int> box) override;
    void close();

    juce::String titleText;
    std::unique_ptr<juce::Component> content;
    juce::TextButton closeButton;
    int contentWidth, contentHeight;
    static constexpr int titleHeight = 44;
};
