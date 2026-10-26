#include "platform/RunLog.h"
#include "platform/Files.h"
#include <windows.h>
#include <cwchar>
#include <string>

namespace bb::RunLog {

void Write(std::wstring_view event)
{
    constexpr size_t Keep = 100;
    std::wstring path = RoamingAppData() + L"\\BlinkBuddy\\runs.log";
    std::wstring text = ReadAllText(path);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    text += stamp;
    text += event;
    text += L'\n';
    // Only the last lines are kept.
    size_t lines = 0, cut = text.size();
    for (size_t i = text.size(); i-- > 0;)
        if (text[i] == L'\n' && ++lines > Keep) {
            cut = i + 1;
            break;
        }
    if (lines > Keep) text.erase(0, cut);
    WriteAllText(path, text);
}

} // namespace bb::RunLog
