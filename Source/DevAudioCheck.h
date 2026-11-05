#pragma once

// 開発用: オーディオまわりの確認 (Debug ビルドのみ)。音は出さず、デバイスも開かない
//   "Guitar Practice.exe" --audio-check <出力フォルダ> [--nam <モデル.nam>] [--ir <IR.wav>]
//
// 1. 使えるオーディオデバイスの種類と名前を一覧にする (ASIO が組み込まれ、ドライバが見えているか)
// 2. NAM のモデルを読み込み、48 kHz・256 サンプルのブロックで 10 秒分を処理して、
//    1ブロックにかかる時間が、間に合う時間 (256 / 48000 = 5.33 ms) に収まるかを測る
// AI はギターの音を聞けないので、「音が出る準備ができているか」を数字で確かめるために使う
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#if JUCE_DEBUG
namespace DevAudioCheck
{
    constexpr const char* commandLineFlag = "--audio-check";

    // 結果を <出力フォルダ>/audio-check.txt に書き、同じ内容を返す
    juce::String run (const juce::String& commandLine);

    // プラグインの画面のウィンドウが開くかを確かめる (画面に数秒だけウィンドウが出る。マウスは操作しない)
    //   "Guitar Practice.exe" --plugin-window-test <出力フォルダ> --plugin <プラグイン.vst3>
    // 練習画面と同じ部品 (PluginToneCard と、エンジンの非同期の読み込み) を使う。
    // 結果は <出力フォルダ>/plugin-window.txt。終わったら onFinished を呼ぶ (アプリを終える)
    constexpr const char* pluginWindowTestFlag = "--plugin-window-test";

    // メトロノームのクリックが、いつ鳴るかを数える (エンジンの処理を直接動かす。音は出さない)
    //   "Guitar Practice.exe" --metronome-check <出力フォルダ>   → metronome-check.txt
    constexpr const char* metronomeCheckFlag = "--metronome-check";
    juce::String runMetronomeCheck (const juce::String& commandLine);
    std::unique_ptr<juce::Timer> startPluginWindowTest (const juce::String& commandLine, std::function<void()> onFinished);
}
#endif
