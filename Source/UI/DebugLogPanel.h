#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 動作ログとクラッシュの記録を見る画面 (設定画面の「動作ログ・クラッシュの記録を見る…」から開く)
//   - 前回の終わり方 (正しく終了したか)
//   - Windows が残したクラッシュダンプの一覧 (%LOCALAPPDATA%\CrashDumps)
//   - 動作ログ (app.log) の最後の部分。落ちたときは、落ちた場所と呼び出しの流れが書かれている
// 作者が「なぜ落ちたか」を見るため、また AI に調べてもらうときに中身をコピーして渡せるようにするため
class DebugLogPanel final : public juce::Component
{
public:
    DebugLogPanel();

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 860, preferredHeight = 620;

private:
    void reload();

    juce::Label heading, summary;
    juce::TextEditor text;
    juce::TextButton reloadButton, copyButton, logFolderButton, dumpFolderButton;
};
