#pragma once

// アプリが落ちたときに、落ちた場所を動作ログ (app.log) に書く (Windows のみ)
//   - 例外の種類 (例: c0000005 = 読み書きしてはいけないメモリに触れた)
//   - 落ちた場所のモジュール (アプリ本体か、プラグインの .vst3 か) と、その中の位置
//   - 呼び出しの流れ (JUCE の SystemStats::getStackBacktrace)
// windows.h を読むと JUCE の名前と衝突することがあるので、WindowOwner と同じく別のファイルにしている。
// 注意: プラグインが自分で即座に終わらせる種類の落ち方 (c0000409 など) は、ここを通らずに終わることがある。
//       その場合も、次の起動で「前回は正しく終了していません」と記録される (AppLog::noteStart)
namespace CrashLog
{
    void install();
}
