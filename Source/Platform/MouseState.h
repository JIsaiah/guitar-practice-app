#pragma once

// マウスのボタンの、今この瞬間の状態 (Windows のみ)
// どの窓の上で押されたかに関係なく分かる (タブ譜・YouTube の WebView の上で押しても)。
// ラジアルメニュー (左右のボタンを同時に押し続ける) を、画面のどこからでも開けるようにするために使う。
// windows.h を読むと JUCE の名前と衝突することがあるので、WindowOwner と同じく別のファイルにしている
namespace MouseState
{
    bool isLeftDown();
    bool isRightDown();
    bool isMiddleDown();   // 真ん中のボタン (ホイールを押し込む)。トラックボールでもラジアルメニューを開けるように

    // マウスのカーソルを、次にマウスを動かすまで隠す (Windows の SetCursor)。
    // タブ譜・YouTube のページ (WebView) の上では、ページの CSS でカーソルを消しても、
    // マウスを動かすまで画面のカーソルが変わらないため、Windows に直接「カーソル無し」を頼む。
    // 動かすと、その下の窓がいつものカーソルに戻す
    void hideCursorUntilMoved();
}
