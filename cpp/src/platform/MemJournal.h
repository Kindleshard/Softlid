#pragma once
#include <string>

// Diagnostics ("--memlog <file>"): a line in the file whenever BlinkBuddy's memory, threads or loaded libraries change
// noticeably, with what happened just before - to find what each feature costs in a real setup. Not started (the
// usual case), every call does nothing.
namespace bb::MemJournal {

void Start(const std::wstring& file);
void Note(const wchar_t* what);   // something happened: the settings opened, a reminder showed...
void Check();                     // once a second: a line if anything changed since the last one

} // namespace bb::MemJournal
