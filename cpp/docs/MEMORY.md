# Memory

Measured on Windows 11 at 150% scaling, BlinkBuddy idle in the tray. **Private** memory is what the app itself
uses (the honest number); the **working set** also counts pages of Windows' own libraries that every program
shares.

| Version | Private | Working set | Threads | Idle CPU |
|---|---|---|---|---|
| C# (.NET Framework 4.8), all features | 24.6 MB | 15.6 MB | 16 | ~0% |
| C++ chunk 1 (tray, settings, translations) | 1.8 MB | 11.7 MB | 4 | 0% |

Updated after each chunk.
