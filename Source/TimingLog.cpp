#include "TimingLog.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

double TimingLog::processStart()
{
    static const double value = []
    {
       #if JUCE_WINDOWS
        FILETIME creation {}, exitTime {}, kernel {}, user {}, current {};
        if (GetProcessTimes (GetCurrentProcess(), &creation, &exitTime, &kernel, &user))
        {
            GetSystemTimeAsFileTime (&current);
            const auto toMs = [] (FILETIME t)
            {
                return (double) ((((juce::uint64) t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10000.0;  // 100ns 単位 → ms
            };
            return now() - (toMs (current) - toMs (creation));
        }
       #endif
        return now();
    }();
    return value;
}

juce::File TimingLog::getFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GuitarPractice").getChildFile ("timing.log");
}

void TimingLog::mark (const juce::String& label, double startMs)
{
    const auto line = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  "
                    + label + ": " + juce::String (now() - startMs, 1) + " ms";
    DBG ("Timing: " << line);

    const auto file = getFile();
    file.getParentDirectory().createDirectory();
    file.appendText (line + "\n", false, false, "\n");
}
