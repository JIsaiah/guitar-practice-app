#pragma once

// セッション画面の操作。入力元 (マウス、キーボード、将来のフットスイッチ) と切り離すため、
// すべての操作をこのコマンドとして SessionScreen::perform() に渡す (設計書 5.1)
enum class Command
{
    togglePlay,
    rewind,
    fastForward,
    tapTempo,
    setLoopStart,
    setLoopEnd,
    toggleLoop,
    toggleMetronome,
    toggleTuner,
    save,
    backToList
};
