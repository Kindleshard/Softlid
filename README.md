# BlinkBuddy

Eye care that lives in the Windows tray: blink and break reminders, a screen filter, a reading strip, a focus
timer and more. It is light: about **3 MB** of memory while it waits in the tray.

- `cpp/`: the current version, in C++ and Direct2D. This is what gets built and shipped.
- `csharp/`: the original version (C#, .NET Framework 4.8), kept as the reference the rewrite was checked against.

## What it does

- **Blink reminders**: a small eye that blinks with you, or soft eyelids across the screen.
- **Breaks**: look-away breaks every 20 minutes and stand-up breaks every hour. A break can be a simple card, guided
  eye exercises with a liquid eye to follow, or a stretch coach. The steps can be spoken.
- **Screen filter**: brightness below what the display allows, a blue light filter that follows sunset and sunrise
  where you are, colour effects (grayscale, sepia, e-ink, red night), invert, and the display's real backlight over
  DDC/CI.
- **Tone**: calms bright areas and deepens darks. The Standard engine works everywhere; the experimental engines are
  Smart (GPU), Smart layer and DesktopLUT. A visibility guard keeps dark details readable, and a test lab compares
  them all.
- **Modes and app rules**: one-click modes (Reading, Movie, Editing, ...), and a mode per app that switches when that
  app comes to the front.
- **Extras**: a reading strip that dims everything but the line you read, a focus timer, film grain, mini mode (a
  small icon that stays on top), keyboard shortcuts, and keeping the PC awake.
- **21 languages**, a Simple and an Advanced view, and dark title bars and menus.

Settings are kept in `%AppData%\BlinkBuddy\settings.ini`.

## Getting it

Nothing is published yet. To get BlinkBuddy, build it yourself (below).

- **Installer** (`BlinkBuddy-<version>-setup.exe`): installs for your user only, with no administrator rights, into
  `%LocalAppData%\Programs\BlinkBuddy`. It adds a Start menu entry and can start BlinkBuddy when you sign in.
  Uninstalling keeps your settings.
- **Portable** (`BlinkBuddy-<version>-portable.zip`): unzip anywhere and run `BlinkBuddy.exe`. The `voice` folder
  next to it holds the spoken break steps; without it, Windows' own voice reads them.

It needs Windows 10 or 11, 64-bit. There is nothing else to install: the C runtime is built into the exe.

## Building

You need Visual Studio 2022 or its Build Tools, with the "Desktop development with C++" workload.

```bash
powershell -ExecutionPolicy Bypass -File cpp/build.ps1
```

`build.ps1` puts `BlinkBuddy.exe` and its `voice` folder in `cpp/bin/`.

```bash
powershell -ExecutionPolicy Bypass -File cpp/release.ps1
```

`release.ps1` makes a release in `cpp/dist/`: the exe on its own, the portable zip and, if
[Inno Setup 6](https://jrsoftware.org/isinfo.php) is installed, the installer from `cpp/installer/BlinkBuddy.iss`.
Nothing is uploaded.

The version number lives in `cpp/res/BuildVersion.h`. Two constants in `cpp/src/core/Version.h` turn on the parts
that need somewhere to point:

- `ReleaseRepo` (`owner/name` on GitHub): **Options > Check for updates** and the weekly check look at that repo's
  latest release. A newer `BlinkBuddy.exe` attached to it is downloaded, swapped in and restarted. Until it is set,
  the Options page says there is no release channel.
- `DonateUrl`: shows a **Support...** button on the Options page.

Diagnostics:

- `--offscreen` opens the settings off the screen, for screenshots.
- `--ui-gallery` shows every control.
- `--preview-break`, `--preview-exercises`, `--preview-stretch` and `--preview-blink` show those once.

## What it connects to

- GitHub, only for the update check, once a week (if a release repo is set and the option is on), or when you
  click Check for updates.
- GitHub, to download DesktopLUT, only when you choose "Download and set up".
- Windows location, only for the sunset-to-sunrise schedule, a few times a day.

Nothing else leaves the PC.

## Memory

Measured on Windows 11 at 150% scaling. Private memory is what the app itself uses.

| | Tray, idle | Settings open |
|---|---|---|
| C# version | 24.6 MB | — |
| C++ version 1.0.0 | 2.9 MB | 10.2 MB |

When the settings window closes, its working set drops back to about 1 MB. Details for every step of the rewrite
are in [cpp/docs/MEMORY.md](cpp/docs/MEMORY.md).

## DesktopLUT

The DesktopLUT tone engine uses [DesktopLUT](https://github.com/sup3rflyer/DesktopLUT), a separate free program
that applies a colour table inside Windows. BlinkBuddy only starts, watches and stops it, with a small helper that
runs as administrator. Every safeguard stops DesktopLUT and puts BlinkBuddy's own filter back:

- a 15-second check after switching on (and after each Windows update)
- a watchdog for crashes
- the panic key **Ctrl+Alt+Shift+L**
