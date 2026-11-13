#include "TitleBar.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dwmapi.h>
 #include <uxtheme.h>
 #pragma comment (lib, "dwmapi.lib")
 #pragma comment (lib, "uxtheme.lib")
#endif

void TitleBar::setColours (void* window, std::uint32_t captionArgb, std::uint32_t textArgb, bool dark)
{
   #ifdef _WIN32
    if (window == nullptr)
        return;

    const auto hwnd = static_cast<HWND> (window);

    // 番号は Windows の決まり (古い SDK には名前が無いので、数字で書く)
    constexpr DWORD useImmersiveDarkMode = 20;   // Windows 10 (20H1) 以降: 帯を暗い色にする
    constexpr DWORD borderColour  = 34;          // Windows 11 以降: 枠の色
    constexpr DWORD captionColour = 35;          // Windows 11 以降: 帯の色
    constexpr DWORD textColour    = 36;          // Windows 11 以降: 題名の文字の色

    // Windows の色 (COLORREF) は 0x00BBGGRR の並び。juce の 0xAARRGGBB から並べ替える
    const auto toColorRef = [] (std::uint32_t argb) -> COLORREF
    {
        return RGB ((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff);
    };

    const BOOL darkValue = dark ? TRUE : FALSE;
    DwmSetWindowAttribute (hwnd, useImmersiveDarkMode, &darkValue, sizeof (darkValue));

    // Windows 10 では、下の3つは失敗する (何も変わらない) だけなので、結果は見ない
    const auto caption = toColorRef (captionArgb);
    const auto text = toColorRef (textArgb);
    DwmSetWindowAttribute (hwnd, captionColour, &caption, sizeof (caption));
    DwmSetWindowAttribute (hwnd, borderColour, &caption, sizeof (caption));
    DwmSetWindowAttribute (hwnd, textColour, &text, sizeof (text));
   #else
    (void) window;
    (void) captionArgb;
    (void) textArgb;
    (void) dark;
   #endif
}

void TitleBar::hideIconAndTitle (void* window)
{
   #ifdef _WIN32
    if (window == nullptr)
        return;

    // Windows のテーマの機能で、帯の題名とアイコンだけを描かないようにする (ウィンドウの名前そのものは残す)
    WTA_OPTIONS options {};
    options.dwFlags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
    options.dwMask = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
    SetWindowThemeAttribute (static_cast<HWND> (window), WTA_NONCLIENT, &options, sizeof (options));
   #else
    (void) window;
   #endif
}
