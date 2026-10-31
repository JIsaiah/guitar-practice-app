#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Model/SessionStore.h"

// 画面の切り替えを担当する
//   セッション一覧 ─(新規)→ セッション作成 ─(練習を始める)→ セッション画面
//        ↑   └────────(開く)──────────────────────────────┘    │
//        └─────────────────(◀ 一覧)─────────────────────────────┘
class MainComponent final : public juce::Component
{
public:
    MainComponent (AudioEngine& engine, SessionStore& store);

    void resized() override;

private:
    void showList();
    void showSetup();
    void showSession (Session session);
    void openAudioSettings();

    // 画面のボタン処理の途中でその画面を破棄しないよう、切り替えは次のメッセージで行う
    void navigate (std::function<void (MainComponent&)> action);
    void setScreen (std::unique_ptr<juce::Component> screen);

    AudioEngine& engine;
    SessionStore& store;
    std::unique_ptr<juce::Component> currentScreen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
