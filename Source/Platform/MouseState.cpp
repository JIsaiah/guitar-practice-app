#include "MouseState.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>

// GetAsyncKeyState: 呼んだ瞬間に、そのキー (ボタン) が押されているか。最上位のビットが 1 なら押されている。
// マウスの左右を入れ替える設定でも、VK_LBUTTON / VK_RBUTTON は「体の左右」のボタンを返す (両方を見るので、どちらでも同じ)
bool MouseState::isLeftDown()  { return (GetAsyncKeyState (VK_LBUTTON) & 0x8000) != 0; }
bool MouseState::isRightDown() { return (GetAsyncKeyState (VK_RBUTTON) & 0x8000) != 0; }

#else
bool MouseState::isLeftDown()  { return false; }
bool MouseState::isRightDown() { return false; }
#endif
