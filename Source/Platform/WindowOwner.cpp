#include "WindowOwner.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

void WindowOwner::setOwner (void* window, void* owner)
{
   #ifdef _WIN32
    if (window != nullptr && owner != nullptr)
        SetWindowLongPtrW (static_cast<HWND> (window), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR> (owner));
   #else
    (void) window;
    (void) owner;
   #endif
}

void WindowOwner::postCloseRequest (void* window)
{
   #ifdef _WIN32
    if (window != nullptr)
        PostMessageW (static_cast<HWND> (window), WM_CLOSE, 0, 0);
   #else
    (void) window;
   #endif
}

std::string WindowOwner::describe (void* window)
{
   #ifdef _WIN32
    auto hwnd = static_cast<HWND> (window);
    if (hwnd == nullptr || ! IsWindow (hwnd))
        return "no window";

    const auto rectText = [] (HWND h)
    {
        RECT r {};
        GetWindowRect (h, &r);
        return std::to_string (r.left) + "," + std::to_string (r.top) + " " + std::to_string (r.right - r.left)
             + "x" + std::to_string (r.bottom - r.top);
    };

    std::string text = "visible=" + std::string (IsWindowVisible (hwnd) ? "yes" : "no")
                     + " minimized=" + (IsIconic (hwnd) ? "yes" : "no")
                     + " rect=" + rectText (hwnd)
                     + " owner=" + (GetWindow (hwnd, GW_OWNER) != nullptr ? "yes" : "no");

    // 子ウィンドウ (孫も含む) を数え、それぞれのクラス名・表示・大きさを書く
    struct Context { std::string lines; int count = 0; decltype (rectText)* describeRect; };
    Context context { {}, 0, &rectText };

    EnumChildWindows (hwnd, [] (HWND child, LPARAM param) -> BOOL
    {
        auto& c = *reinterpret_cast<Context*> (param);
        char className[128] {};
        GetClassNameA (child, className, (int) sizeof (className));
        c.lines += "\n  child: class=" + std::string (className)
                 + " visible=" + (IsWindowVisible (child) ? "yes" : "no")
                 + " rect=" + (*c.describeRect) (child);
        ++c.count;
        return c.count < 20;   // 多すぎるときは途中まで
    }, reinterpret_cast<LPARAM> (&context));

    return text + " children=" + std::to_string (context.count) + context.lines;
   #else
    (void) window;
    return "not supported";
   #endif
}
