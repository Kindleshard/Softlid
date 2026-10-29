#pragma once
#include <string>

// Softlid was called BlinkBuddy until 1.1.0. Moving a copy over from the old name, once, as it starts.
namespace bb::OldName {

// A BlinkBuddy still running is asked to quit (as Exit in its tray menu does), then its folders in %AppData% and
// %LocalAppData% and its "Start with Windows" entry take Softlid's names. Nothing happens when there is no trace of it.
void MoveOver();

// Renames the folder "from" to "to", unless "to" is already there. When something holds a file in it, only the
// settings come along. True when there was something to move.
bool MoveFolder(const std::wstring& from, const std::wstring& to);

} // namespace bb::OldName
