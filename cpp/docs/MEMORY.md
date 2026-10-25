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
| C++ chunk 5 (+ settings window) | tray, idle | 2.3 MB | 13.5 MB | 4 | 0% |
| C++ chunk 5 | settings window open (480×682 units at 150%) | 9.6 MB | 26.6 MB | 4 | 0% |
| C++ chunk 5 | settings window closed again | 9.6 MB | 1.0 MB | 4 | 0% |
| C++ chunk 6 (+ mini mode, focus timer, reading strip, shortcuts) | mini mode, icon resting | 7.3 MB | 22.7 MB | 10 | 0% |
| C++ chunk 8 (+ tone engines, grain, tone lab) | tray, idle, tone off | 2.9 MB | 15.3 MB | 4 | 0% |
| C++ chunk 8 | settings window open | 10.2 MB | 28.0 MB | 4 | 0% |
| C++ chunk 8 | settings window closed again | 10.1 MB | 1.0 MB | 4 | 0% |
| C++ 1.0.0 (chunk 9: + updater, voice clips beside the exe) | tray, idle | 2.9 MB | 15.3 MB | 4 | 0% |
| C++ 1.0.0 | settings window open | 10.2 MB | 28.0 MB | 4 | 0% |
| C++ 1.0.0 | settings window closed again | 10.2 MB | 1.0 MB | 4 | 0% |

Windows are drawn by Direct2D in software (no graphics-card device, which alone would add tens of MB); most of a
window's cost is its drawing buffer and the fonts. When the settings window closes, its pages go back to Windows
(the working set drops to about 1 MB), but some private memory stays: libraries Windows loads for the window - text
input services once a text box has existed, Direct2D's and DirectWrite's own caches - stay loaded until BlinkBuddy
exits. For scale: the C# version measured 42.8 MB private when idle on the same day.

**A correction:** until chunk 5, the C++ app's hidden window failed to be created (a bug from chunk 1), so it had no
tray icon and no 1-second tick; the idle rows for chunks 1-3 were measured without them. Re-measured with the window
working (chunk 5, above), idle is 2.3 MB private - within half a MB of those rows.

The colour filter is Windows' own full-screen colour effect: one 5×5 matrix handed to Windows, so it costs no
memory, windows or drawing at all. Location, backlight and display checks run on short-lived threads that end when
they have answered.

**Voice clips:** with the clips beside the exe (from chunk 9 on), opening the settings first cost 19.4 MB, 9 MB more.
The Blink page asked Windows' media player for each clip's length, which loads its whole playback stack (and a thread)
for good. BlinkBuddy now reads the lengths from the files' own headers, so that stack loads only when a clip plays
during a break.

The tone engines cost nothing until one is used. Standard is a gamma ramp handed to Windows (no memory, no
windows). Smart (GPU), Smart layer and DesktopLUT start their device, layer or guard only when chosen, and stop them
when switched away; they have not been measured running yet, since that changes the whole screen. The idle rise
since chunk 6 (about half a MB) is the larger program and two hidden windows for the tone timers.

Pictures for pop-ups are drawn in software (no graphics-card device) and released when the pop-up hides, so memory
goes back down after each reminder.

Updated after each chunk.
