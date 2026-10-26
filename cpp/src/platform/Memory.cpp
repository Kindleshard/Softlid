#include "platform/Memory.h"
#include <windows.h>

namespace bb {

void TrimMemory()
{
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

} // namespace bb
