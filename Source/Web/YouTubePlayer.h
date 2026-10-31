#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// YouTube の埋め込みプレイヤー (設計書 v0.7 3.2、5.1、実装指示 T5)
//
// - WebView2 (別プロセス) の中で、同梱のページ (Resources/Web/youtube-player.html) を開き、
//   YouTube の公式プレイヤー API で操作する。音は YouTube から Windows の通常の出力に出る (アプリのミキサーは通らない)
// - ページは https://juce.backend/ の住所で開く。パソコン内のファイルとして開くと、
//   YouTube に埋め込み先が伝わらず、再生を拒否される (エラー 153)
// - WebView はアプリの起動中に1つだけ作り、作り直さずに使い回す。
//   一覧画面を表示した直後に prepare() で裏で準備しておき、練習画面では表示するだけにする
// - この部品は MainComponent の子のまま動かさない (親を替えると WebView が作り直されるおそれがあるため)。
//   練習画面は placeOver() で、表示する場所を指定する
class YouTubePlayer final : public juce::Component
{
public:
    struct State
    {
        bool pageReady = false;     // ページを読み込んだ
        bool apiReady = false;      // YouTube の API を読み込んだ (ネットが必要)
        bool videoReady = false;    // 動画を再生できる状態になった
        int playerState = -1;       // YouTube の状態: -1 未開始, 0 終了, 1 再生中, 2 一時停止, 3 読み込み中, 5 頭出し済み
        double time = 0.0, duration = 0.0, rate = 1.0;
        double receivedAtMs = 0.0;  // time を受け取った時刻 (位置の推定に使う)
    };

    YouTubePlayer();
    ~YouTubePlayer() override;

    // WebView を作ってページを読み込む (1回だけ。2回目以降は何もしない)
    void prepare();

    // 練習画面の target の上に表示する。target の位置・大きさが変わったら、もう一度呼ぶ
    void placeOver (juce::Component& target);
    void hide();

    //==== 操作 (メッセージスレッドから) ====
    void load (const juce::String& videoId, double startSeconds);
    void unload();
    void play();
    void pause();
    void seek (double seconds);
    void setRate (double rate);
    void setVolume (double gain);   // ミキサーの値 (0〜1.5)。YouTube の音量は 0〜100 なので、1.0 以上は 100
    void setLoop (double startSeconds, double endSeconds, bool enabled);

    //==== 状態 ====
    const State& getState() const noexcept { return state; }
    bool isPlaying() const noexcept { return state.playerState == 1; }
    double getEstimatedTime() const;   // 最後に届いた位置から、経過時間で進めた推定値

    std::function<void()> onStateChanged;                    // 位置や状態が届くたびに (約50msごと)
    std::function<void (const juce::String& message)> onError;
    std::function<void()> onApiReady;                        // 準備完了 (時間の計測用)

    void resized() override;

    static juce::String describeError (const juce::var& code);

private:
    void handleEvent (const juce::var& event);
    void send (const juce::var& command);

    std::unique_ptr<juce::WebBrowserComponent> browser;
    State state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YouTubePlayer)
};
