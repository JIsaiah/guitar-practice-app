#pragma once

// 開発用: 画面とロゴを、ウィンドウを開かずに PNG に書き出す (Debug ビルドのみ)
//   suguhiku.exe --render-logos <出力フォルダ>
// AI が見た目を確認するときに、マウスを操作したり、作者の設定ファイルを書き換えたりせずに済むようにするため
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_DEBUG
namespace DevSnapshots
{
    constexpr const char* commandLineFlag = "--render-logos";

    // 書き出したファイルの一覧と、ロゴの生成時間を文字列で返す
    juce::String renderAll (const juce::File& folder);
}
#endif
