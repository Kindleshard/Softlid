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
| C++ chunk 3 (+ screen filter, night schedule, modes) | tray, idle, filter warming the screen | 2.2 MB | 12.6 MB | 4 | 0% |
| C++ chunk 4 (+ interface toolkit) | the control gallery open (760×620 units at 150%) | 21.2 MB | 36.5 MB | 11 | 0% |

Windows are drawn by Direct2D in software (no graphics-card device, which alone would add tens of MB); most of a
window's cost is its drawing buffer and the fonts, and it all goes when the window closes. For scale: the C#
version measured 42.8 MB private when idle on the same day.

The colour filter is Windows' own full-screen colour effect: one 5×5 matrix handed to Windows, so it costs no
memory, windows or drawing at all. Location, backlight and display checks run on short-lived threads that end when
they have answered.

Pictures for pop-ups are drawn in software (no graphics-card device) and released when the pop-up hides, so memory
goes back down after each reminder.

Updated after each chunk.
