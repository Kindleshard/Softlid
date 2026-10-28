# Building BlinkBuddy

BlinkBuddy is written in C++ and Direct2D, in `cpp/`. (It began in C#, .NET Framework 4.8; that version, which the
rewrite was checked against, is in the history up to commit acdbee3.)

## Build

You need Visual Studio 2022 or its Build Tools, with the "Desktop development with C++" workload.

```bash
powershell -ExecutionPolicy Bypass -File cpp/build.ps1
```

`build.ps1` puts `BlinkBuddy.exe` and its `voice` folder in `cpp/bin/`. It also builds and runs the tests (`cpp/tests`:
the core logic, the tone pipeline's decisions, settings, shortcuts, the translations and the Night light reader); a
failing test stops the build. `build.ps1 -Arch arm64` builds for ARM64 PCs into `cpp/bin-arm64/` (with Visual Studio's
ARM64 tools, "MSVC ARM64 build tools"; on an x64 PC its tests are built but can't run).

To check that a change leaves the settings window looking exactly as before, `cpp/tools/ui-check.ps1 -Update` saves a
baseline of every view (each tab, scrolled through to its end) and `cpp/tools/ui-check.ps1` compares against it, pixel
by pixel. It runs a diagnostic copy (`--offscreen --settings <file>`) beside a running BlinkBuddy, with settings of its
own, so nothing of yours is touched.

## Release

```bash
powershell -ExecutionPolicy Bypass -File cpp/release.ps1
```

`release.ps1` makes a release in `cpp/dist/`, for x64 and (with the ARM64 tools) ARM64: the exe on its own, the
portable zip and, if [NSIS 3](https://nsis.sourceforge.io) is installed (or unzipped into `C:\Tools\nsis`), the
installer from `cpp/installer/BlinkBuddy.nsi`. It prints each installer's SHA-256 for the winget manifest in
`cpp/installer/winget`. Nothing is uploaded.

The installer installs for the current user only, with no administrator rights, into
`%LocalAppData%\Programs\BlinkBuddy`. It asks whether the spoken steps should come in just one language or all 21;
unattended, `/S /VOICES=all` or `/S /VOICES=<code>` chooses, and an update keeps what was chosen before.

The version number lives in `cpp/res/BuildVersion.h`. Two constants in `cpp/src/core/Version.h` turn on the parts
that need somewhere to point:

- `ReleaseRepo` (`owner/name` on GitHub): **Options > Check for updates** and the weekly check look at that repo's
  latest release. A newer `BlinkBuddy.exe` (`BlinkBuddy-arm64.exe` on ARM64) attached to it is downloaded, swapped in
  and restarted. Until it is set, the Options page says there is no release channel, and the bug report row is hidden.
- `DonateUrl`: shows a **Support** button on the Options page.

## Diagnostics

- `--offscreen` opens the settings off the screen, for screenshots; with `--no-screen` that copy leaves the screen
  filter and tone off the screen, and with `--tray` it starts quietly (mini mode shows, if it's on).
- `--ui-gallery` shows every control (debug builds only: `build.ps1 -Debug`).
- `--preview-break`, `--preview-exercises`, `--preview-stretch` and `--preview-blink` show those once; `--settings
  <file>` gives them settings of their own.

## The README's media

The loops and the link preview are made in `cpp/tools/media` (Chrome and ffmpeg needed): `record.ps1` films BlinkBuddy
over a staged desktop, `shot.ps1` captures its settings window off the screen, and `node render.mjs <loop>` renders
`loops/<loop>.html` into `docs/media/<loop>.avif` (`--clean` without its words, for the grid). The spoken steps are
made by `cpp/tools/make-voices.py` (see its header).

## Memory

Measured on Windows 11 at 150% scaling. Private memory is what the app itself uses.

| | Tray, idle | Settings open |
|---|---|---|
| C# version | 24.6 MB | — |
| C++ version 1.0.0 | 2.9 MB | 10.2 MB |
| C++, one shared drawing device | 1.9 MB | 13.1 MB |

The last row is measured with the window on the monitor; the rows above it measured a window kept off the screen,
which Windows hardly paints, so their "Settings open" figures are too low (the same build measured on the monitor:
16.5 MB). Left alone for a few seconds, the open settings window gives its pages back to Windows: about 0.6 MB of
its own in RAM; mini mode does the same once its icon is still (0.4 MB). When the settings close, the working set
drops back to about 1 MB. Details for every step of the rewrite are in [MEMORY.md](MEMORY.md).

## DesktopLUT

The DesktopLUT tone engine uses [DesktopLUT](https://github.com/sup3rflyer/DesktopLUT), a separate free program
that applies a colour table inside Windows. BlinkBuddy only starts, watches and stops it, with a small helper that
runs as administrator. Every safeguard stops DesktopLUT and puts BlinkBuddy's own filter back:

- a 15-second check after switching on (and after each Windows update)
- a watchdog for crashes
- the panic key **Ctrl+Alt+Shift+L**
