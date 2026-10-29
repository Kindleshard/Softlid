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
input services once a text box has existed, Direct2D's and DirectWrite's own caches - stay loaded until Softlid
exits. For scale: the C# version measured 42.8 MB private when idle on the same day.

**A correction:** until chunk 5, the C++ app's hidden window failed to be created (a bug from chunk 1), so it had no
tray icon and no 1-second tick; the idle rows for chunks 1-3 were measured without them. Re-measured with the window
working (chunk 5, above), idle is 2.3 MB private - within half a MB of those rows.

The colour filter is Windows' own full-screen colour effect: one 5×5 matrix handed to Windows, so it costs no
memory, windows or drawing at all. Location, backlight and display checks run on short-lived threads that end when
they have answered.

**Voice clips:** with the clips beside the exe (from chunk 9 on), opening the settings first cost 19.4 MB, 9 MB more.
The Blink page asked Windows' media player for each clip's length, which loads its whole playback stack (and a thread)
for good. Softlid now reads the lengths from the files' own headers, so that stack loads only when a clip plays
during a break.

The tone engines cost nothing until one is used. Standard is a gamma ramp handed to Windows (no memory, no
windows). Smart (GPU), Smart layer and DesktopLUT start their device, layer or guard only when chosen, and stop them
when switched away; they have not been measured running yet, since that changes the whole screen. The idle rise
since chunk 6 (about half a MB) is the larger program and two hidden windows for the tone timers.

Pictures for pop-ups are drawn in software (no graphics-card device) and released when the pop-up hides, so memory
goes back down after each reminder.

**The settings window, slimmed (after the Resource use section).** Measured with a diagnostic copy, settings open on
the Options page (own pages in RAM, then set aside in all):

| | before | after |
|---|---|---|
| Screen tab, just opened | 7.9 / 8.9 MB | 0.1 / 3.8 MB |
| Options page, scrolled through | 8.1 / 9.2 MB | 1.4 / 4.1 MB |
| ...left alone for 10 seconds | 8.1 / 9.2 MB | 0.5 / 4.1 MB |
| ...with the Resource use meter showing | 11.9 / 13.3 MB | 0.6 / 4.1 MB |

Three changes. A probe with a bare Direct2D window and 40 lines of text came to 9 MB set aside, 6.6 MB in RAM: about
all of what the settings window cost, so the cost was the drawing engine, not Softlid's own data.
- Windows are now drawn into their own device context, bound to just the area being repainted, instead of through a
  window target (which keeps a whole-window buffer plus a copy to present from, and touches both on every paint, even
  an empty one: 2.9 MB). A small repaint now touches about 1.5 MB instead of 3.3 MB. Screenshots of every settings
  page and of the control gallery are pixel for pixel the same.
- Left alone for 4 seconds, the settings window hands its pages back to Windows (as when it closes). Hidden controls
  (another tab's) no longer ask for repaints, so nothing pulls them back while it sits there.
- The Resource use meter reads the graphics card from the graphics kernel's per-process engine statistics (what Task
  Manager's figures come from) instead of the performance counters, which cost 3.8 MB just to open.

(Screenshots inflate these figures: a capture makes the window paint itself whole into a screenshot buffer, about
10 MB more until the next trim, with either kind of drawing.)

**A correction, and the real setup (with a memory journal).** The figures above came from a diagnostic copy whose
window sat off every monitor - and Windows paints little of a window no monitor shows, so the drawing engine hardly
ran. Measured where the window really is (on the monitor, cloaked: drawn but never seen, "--offscreen --cloaked"),
the settings window cost 16.5 MB set aside with 12 threads. "--memlog <file>" now writes a line whenever memory,
threads or libraries change, with what just happened, which is how the real setup was followed: the screen filter
(the magnifier) costs next to nothing; mini mode and the settings window cost what Direct2D's software device costs.

Each Direct2D DC render target brings up its own software device (WARP) with about eight worker threads. Now one
software device, made without internal threading, draws every window and picture on the app's own thread, and goes
when the last one closes; mini mode also gives its pages back to Windows a few seconds after it goes still. Screenshots
of every settings page, the control gallery and the mini icon are pixel for pixel the same. With this PC's settings,
cloaked, after 20 seconds:

| | before | after |
|---|---|---|
| Mini mode, still | 5.7 MB set aside, 4.0 MB in RAM, 8 threads | 5.6 MB, 0.4 MB, 5 threads |
| Mini mode, breathing without a pause | 7.8 MB, 5.1 MB, 12 threads | 7.1 MB, 4.4 MB, 5 threads |
| Settings open, left alone | 16.5 MB, 0.6 MB, 12 threads | 13.1 MB, 0.6 MB, 5 threads |
| Tray only | 1.9 MB, 1.4 MB, 4 threads | the same |

What stays: Direct2D's heap keeps its high-water mark (set aside) while a window is open - its glyph and shape caches
can be emptied, but the heap keeps the pages reserved; RAM is what the trims give back. A breathing icon redraws 30
times a second, so it keeps about 4 MB in RAM and a few percent of one core while it breathes.

**Before the first release (keyboard and screen readers, the first-run tour, voices in 19 languages).** Cloaked diagnostic copies, this
PC's settings, after 8 seconds, then CPU over 30 seconds:

| State | Private | Working set | Threads | CPU (of one core) |
|---|---|---|---|---|
| Tray only | 2.0 MB | 11.9 MB | 1 | 0.00% |
| Mini mode, the countdown ring | 5.4 MB | 2.1 MB | 4 | 0.16% |
| Mini mode, the focus timer working | 5.5 MB | 5.3 MB | 4 | 0.26% (was 2.81%: its highlight ran 30 times a second for the whole work period; now one run every 20 s, or on and on under the pointer) |
| Settings open | 11.2 MB | 2.8 MB | 7 | 0.00% |
| Settings open, the night bar's waving line showing | 11.7 MB | 2.8 MB | 7 | 0.16% |
| The first-run tour open | 8.2 MB | 27.9 MB | 7 | 0.00% |

Screen readers cost nothing until one asks: UI Automation (uiautomationcore.dll) is delay-loaded, and its objects are
made on the first question. The voice clips are 22 kHz mono MP3s at 32 kbit/s, about 16 KB a line, 7 MB for 19
languages on disk; one plays at a time and none is in memory otherwise.

Updated after each chunk.
