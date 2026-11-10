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
}
