# BlinkBuddy in C++ — the plan

The C# version (in `../csharp`) is the reference: same features, same look, same settings file. The C++
version exists to be small and light — a few MB of memory instead of ~15 — while staying easy to read.

## Goals

- **Light:** idle at ~0% CPU and a few MB of memory. Graphics (Direct2D) are created only while a window that
  needs them is open, and released when it closes.
- **One file:** a single portable `BlinkBuddy.exe` with the C runtime linked in (nothing to install), plus an
  optional installer.
- **Readable:** small files with one job each, names that say what things are, comments that say *why*.
- **Compatible:** reads and writes the same `%AppData%\BlinkBuddy\settings.ini`, uses the same translations, and
  the same single-instance names, so the C# and C++ versions never run side by side.

## Layout

```
cpp/
  CMakeLists.txt     the build (MSVC, C++20, static runtime)
  build.ps1          one command: finds Visual Studio, configures, builds into bin/
  docs/              this plan, and how the code is organised
  res/               icon, manifest, translations (embedded in the exe)
  src/
    main.cpp         entry point: one copy at a time, then the app
    app/             the tray app: message loop, tray icon and menu, the 1-second tick
    core/            plain logic with no windows: settings, translations, (later) schedules and timers
    platform/        thin wrappers over Windows: handles, dark mode, paths
    ui/              (chunk 4) the Direct2D toolkit: window, layout, controls
    features/        (chunks 2+) reminders, screen filter, reading strip, mini mode, ...
```

**Rules of thumb:** `core/` never touches a window, so it can be tested on its own. `platform/` hides raw Win32
behind small types. A file stays under ~400 lines; when it grows past that, it has more than one job.

## Chunks

Each chunk builds and runs on its own, ends in a commit, and leaves the app usable.

| # | Chunk | Done when |
|---|---|---|
| 1 | **Foundation:** build, entry point, single instance, settings (load/save/migrate), translations, tray icon and menu, the tick | It starts in the tray, reads your settings, and exits cleanly |
| 2 | **Reminders:** blink eye popup, look-away and stand-up cards, idle and full-screen detection, chime, pause | Reminders work like the C# version (simple cards) |
| 3 | **Screen:** colour-effect filter (brightness, warmth, effects), night schedule and location, modes and per-app rules, real backlight, visibility guard | The screen filter matches the C# version |
| 4 | **UI toolkit:** Direct2D window with per-monitor DPI, a layout grid, and the controls (tabs, toggle, slider, choices, drop-down, buttons, pickers, colour wheel) | A test window shows every control |
| 5 | **Settings window:** the five pages, folding sections, Simple/Advanced | All settings can be changed |
| 6 | **Extras:** mini mode, focus timer, reading strip, keyboard shortcuts, the app-rules dialog | Feature parity for these |
| 7 | **Guided breaks:** liquid eye animation, eyelids, stretch coach, voice | Breaks look and sound like the C# version |
| 8 | **Tone engines:** standard curve, Smart (GPU), Smart layer, DesktopLUT with its guard, tone lab, grain | Tone matches |
| 9 | **Release:** updater, installer + portable exe, donate link, README, memory numbers | Ready to share |

## Measuring "light"

After each chunk: working set and private bytes at idle (tray only), with the settings window open, and during
a break — written into `docs/MEMORY.md` so the numbers can be shown honestly.
