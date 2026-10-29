#pragma once
#include <string_view>

// A short history of when Softlid started and why it ended, in %AppData%\Softlid\runs.log (the last 100 lines).
// A start with no "exited" line before it means the copy before was ended from outside (or crashed).
namespace bb::RunLog {

void Write(std::wstring_view event);

} // namespace bb::RunLog
