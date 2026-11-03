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
