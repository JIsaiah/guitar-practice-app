#pragma once

#include <juce_core/juce_core.h>

// 起動や画面遷移にかかった時間を記録する (実装指示 T5)
// %APPDATA%\GuitarPractice\timing.log に1行ずつ追記し、DBG にも出す (Debug ビルドだけ。配布版 (Release) では何もしない)
namespace TimingLog
{
    // 今の時刻 (ミリ秒)。Time::getMillisecondCounterHiRes() と同じ基準
    inline double now() { return juce::Time::getMillisecondCounterHiRes(); }

    // プロセスが作られた時刻を、now() と同じ基準に直したもの。
    // アプリの処理が始まる前 (実行ファイルの読み込みなど) の時間も含めるため、OS に聞く
    double processStart();

    juce::File getFile();

    // 「label: 〇〇 ms」を記録する。startMs は now() で測った開始時刻
    void mark (const juce::String& label, double startMs);
}
