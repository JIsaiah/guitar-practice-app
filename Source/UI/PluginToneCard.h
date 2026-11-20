#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Audio/AudioEngine.h"

// VST3 プラグインのカード。練習画面の右パネルの「音色」の枠の中 (音色のボタンの下) に出す
//
// 作者のレイアウト (2026年11月): 1行目にプラグインの名前、2行目に
//   「プラグインを開く」(左の大きな枠に出す) ・「VSTを選択」・「別ウィンドウで開く」
// - 編集画面 (エディタ) は、開く場所 (左の大きな枠 = MainView か、別のウィンドウ) の中に最初から作る。
//   VST3 の画面は Windows の窓として親に結び付くので、途中で親を変えると表示されないことがあるため。
//   場所を変えるときは、編集画面を消してから、新しい場所に作り直す
// - 編集画面は同時に1つだけ (Archetype Misha Mansoor X は2つ目の画面で落ちる)
// - 別のウィンドウは、アプリのウィンドウを持ち主にする (アプリの後ろに隠れない。WindowOwner)
// - 消す順番は「編集画面 → ウィンドウ」(deleteEditor)。逆にするとプラグインが落ちることがある
// - 編集画面は、プラグインより先に消す必要がある。エンジンがプラグインを差し替える前に deleteEditor() を呼んでもらう
class PluginToneCard final : public juce::Component
{
public:
    explicit PluginToneCard (AudioEngine& engine);
    ~PluginToneCard() override;

    enum class State { notFound, loading, error, ready };
    void setState (State newState, const juce::String& message = {});

    // 編集画面を出している場所
    enum class Place { none, main, window };

    std::function<void()> onChooseFile;   // 「VSTを選択」
    std::function<void()> onRescan;       // 「探し直す」
    std::function<void()> onSizeChanged;  // カードの高さが変わった (練習画面が並べ直す)
    std::function<void()> onPlaceChanged; // 編集画面を出す場所が変わった (練習画面は、左の大きな枠のタブ譜・動画を出す・隠す)
    std::function<void (const juce::String&)> onStatus;   // 状態の帯に出す知らせ

    // エンジンが持っているプラグインの編集画面を作り直す (差し替えの前に開いていた場所で開き直す) / 消す
    void refreshEditor();
    void deleteEditor();   // 差し替えのため: 開いていた場所を覚えておき、refreshEditor で開き直す
    void closeEditor();    // 閉じる: 開き直さない
    bool hasEditor() const noexcept { return editor != nullptr; }

    // 編集画面を開く。まだプラグインが無ければ (読み込み中)、できたときに開く
    void requestOpen (Place where);
    void requestOpenWindow() { requestOpen (Place::window); }
    // ウィンドウを閉じる (閉じるボタンと同じ)。編集画面ごと、次のメッセージで消す
    void closeWindow();

    Place getPlace() const noexcept { return place; }
    bool isMainOpen() const noexcept { return place == Place::main; }

    // 左の大きな枠に重ねる部品 (練習画面が子にして、タブ譜の枠と同じ場所に置く)。
    // 上の帯にプラグインの名前と「閉じる」、その下の真ん中に編集画面
    juce::Component& getMainView() noexcept;

    // この幅で並べたときに必要な高さ
    int getPreferredHeight() const;

    // 確認用 (開発用のテストで使う): プラグインの画面のウィンドウが出ているか、どこに出ているか
    bool isWindowShowing() const;
    juce::Rectangle<int> getWindowScreenBounds() const;
    juce::Rectangle<int> getEditorSize() const;
    void* getWindowNativeHandle() const;   // Windows の窓の番号 (WindowOwner::describe で調べる)

    void resized() override;
    void visibilityChanged() override;

private:
    class PluginWindow;
    class MainView;

    void openAt (Place where);
    bool createEditor (juce::Component* parent);   // 編集画面を作る (parent が null ならウィンドウの中に)
    bool createEditorWindow();
    void setPlace (Place newPlace);
    void updateControls();

    AudioEngine& engine;
    State state = State::notFound;

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;
    std::unique_ptr<MainView> mainView;
    Place place = Place::none;
    Place reopenPlace = Place::none;   // 差し替え・読み込み中: できたときに開く場所
    juce::String pluginName;

    juce::Label titleLabel, messageLabel;
    juce::TextButton downloadButton, chooseButton, rescanButton, openMainButton, openWindowButton;

    static constexpr int headerHeight = 26, buttonHeight = 30, messageHeight = 64;   // 案内の文は3行まで
};
