#include "CrashLog.h"
#include <juce_core/juce_core.h>
#include "../AppLog.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{
   #ifdef _WIN32
    // 落ちた場所が、どのモジュール (exe や dll、.vst3) の中か
    juce::String describeAddress (void* address)
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  static_cast<LPCWSTR> (address), &module) || module == nullptr)
            return "0x" + juce::String::toHexString ((juce::pointer_sized_int) address) + " (モジュール不明)"_jp;

        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW (module, path, MAX_PATH);
        const auto offset = (juce::pointer_sized_int) address - (juce::pointer_sized_int) module;
        return juce::File (juce::String (path)).getFileName() + " + 0x" + juce::String::toHexString (offset);
    }
   #endif

    void handleCrash (void* platformInfo)
    {
        juce::String text ("!! 落ちました"_jp);

       #ifdef _WIN32
        if (auto* info = static_cast<EXCEPTION_POINTERS*> (platformInfo); info != nullptr && info->ExceptionRecord != nullptr)
        {
            const auto* record = info->ExceptionRecord;
            text << ": 例外 0x"_jp << juce::String::toHexString ((int) record->ExceptionCode)
                 << " 場所 "_jp << describeAddress (record->ExceptionAddress);
        }
       #else
        juce::ignoreUnused (platformInfo);
       #endif

        AppLog::write (text);
        AppLog::write ("呼び出しの流れ:\n"_jp + juce::SystemStats::getStackBacktrace());
    }
}

void CrashLog::install()
{
    juce::SystemStats::setApplicationCrashHandler (handleCrash);
}
