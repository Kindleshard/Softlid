# Memory

Measured on Windows 11 at 150% scaling. **Private** memory is what the app itself uses (the honest number); the
**working set** also counts pages of Windows' own libraries that every program shares.

| Version | State | Private | Working set | Threads | Idle CPU |
|---|---|---|---|---|---|
| C# (.NET Framework 4.8), all features | tray, idle | 24.6 MB | 15.6 MB | 16 | ~0% |
| C++ chunk 1 (tray, settings, translations) | tray, idle | 1.8 MB | 11.7 MB | 4 | 0% |
| C++ chunk 2 (+ reminders) | tray, idle | 1.9 MB | 12.2 MB | 4 | 0% |
| C++ chunk 2 | during a blink | 5.8 MB | — | — | — |
| C++ chunk 2 | break card showing | 8.5 MB | — | — | — |

Pictures for pop-ups are drawn in software (no graphics-card device) and released when the pop-up hides, so memory
goes back down after each reminder.

Updated after each chunk.
