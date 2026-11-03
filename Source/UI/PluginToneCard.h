#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Audio/AudioEngine.h"

// TONE3000 (VST3 プラグイン) のカード。練習画面の右パネルで、音色とミキサーの間に出す
//
// - 今は、プラグインの編集画面 (エディタ) を **別のウィンドウ** で開く (設計書 v0.7 4.x の方針)。
//   カードには、プラグインの名前・「プラグインの画面を開く」・見つからないときの案内を出す
//   (作者の依頼で、まず別のウィンドウで動きを確かめる。最終的には練習画面の中に収めたい)
// - 編集画面は、最初から別のウィンドウの中に作る。VST3 の画面は Windows の窓として親に結び付くので、
//   途中で親を変える (カードからウィンドウへ移す) と表示されないことがあるため
// - 別のウィンドウは、アプリのウィンドウを持ち主にする (アプリの後ろに隠れない。WindowOwner)
// - 大きさを変えられるプラグイン (Neural DSP など) は、ウィンドウの大きさを変えられる
// - 編集画面は、プラグインより先に消す必要がある。エンジンがプラグインを差し替える前に deleteEditor() を呼んでもらう
class PluginToneCard final : public juce::Component
{
public:
    explicit PluginToneCard (AudioEngine& engine);
    ~PluginToneCard() override;

    enum class State { notFound, loading, error, ready };
    void setState (State newState, const juce::String& message = {});

    std::function<void()> onChooseFile;   // 「VST3 を選ぶ…」
    std::function<void()> onRescan;       // 「探し直す」
    std::function<void()> onSizeChanged;  // カードの高さが変わった (練習画面が並べ直す)

    // エンジンが持っているプラグインの編集画面を作り直す / 消す
    void refreshEditor();
    void deleteEditor();
    bool hasEditor() const noexcept { return editor != nullptr; }

    // プラグインの画面のウィンドウを開く。まだ編集画面がなければ、できたときに開く
    void requestOpenWindow();

    // 練習画面の中に埋め込む形 (今は使っていない) のための入り口。今は常に別のウィンドウ
    void setAvailableSpace (int, int) {}
    bool isUsingWindow() const noexcept { return true; }
    void setEditorAllowed (bool) {}
    juce::Rectangle<int> getEditorNaturalSize() const noexcept { return {}; }
    juce::Rectangle<int> getEditorBounds() const noexcept { return {}; }

    // この幅で並べたときに必要な高さ
    int getPreferredHeight() const;

    void resized() override;
    void visibilityChanged() override;

private:
    class PluginWindow;

    void openWindow();
    void updateControls();

    AudioEngine& engine;
    State state = State::notFound;

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;
    bool openWhenReady = false;
    juce::String pluginName;

    juce::Label titleLabel, messageLabel;
    juce::TextButton downloadButton, chooseButton, rescanButton, openWindowButton;

    static constexpr int headerHeight = 28, messageHeight = 64;   // 案内の文は3行まで
};
