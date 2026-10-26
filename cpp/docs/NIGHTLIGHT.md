# Windows Night light: how it is stored

Windows has no API for Night light. Its state and settings are binary values in the registry, decoded here by
changing one setting at a time and comparing the bytes. Tested on Windows 11 build 26300, 2026-10-26.

Everything below is undocumented: read it carefully and treat anything unexpected as "unknown".

## Where

`HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\DefaultAccount\Current`, then a key and a
subkey of the same name (without `default$`), value `Data` (REG_BINARY):

| Key | What |
|---|---|
| `default$windows.data.bluelightreduction.bluelightreductionstate` | on or off right now |
| `default$windows.data.bluelightreduction.settings` | schedule and strength |
| `{guid}$windows.data.bluelightreduction.bluelightreductionstateperdevice` | per-display copy: **not used yet** |
| `{guid}$windows.data.bluelightreduction.settingsperdevice` | per-display copy: **not used yet** |

The per-display copies exist on this build but did not change once while every setting was changed. Read the
global ones; look at the per-display ones only if the global ones are missing, and then flag the result as a guess.

## Layout

```
43 42 01 00                      header
0A 02 01 00 2A 06 <varint>       last written, in Unix seconds
2A 2B 0E <length>                length of what follows (up to the trailing zeros)
43 42 01 00 <fields...>          the fields, in this order when present
00 00 00 00                      end
```

Numbers are little-endian base-128 varints (7 bits a byte, high bit = more). A time is `0E <hour>`, then
`2E <minute>` only when the minute isn't 0, then `00`.

## Settings fields

| Bytes | Meaning | Absent means |
|---|---|---|
| `02 01` | the schedule is on | schedule off |
| `C2 0A 00` | Set hours | Sunset to sunrise |
| `CA 14 <time>` | Set hours: turn on (e.g. `0E 16 2E 1E 00` = 22:30) | |
| `CA 1E <time>` | Set hours: turn off (`0E 07 00` = 07:00) | |
| `CF 28 <varint>` | colour temperature × 2 (zigzag): 13000 = 6500 K | |
| `CA 32 <time>` | sunset, as Windows worked it out (18:49) | |
| `CA 3C <time>` | sunrise (06:37) | |

Strength in Settings maps linearly onto the colour temperature: **K = 6500 − 53 × strength**. Measured: 0 = 6500 K,
50 = 3850 K, 100 = 1200 K.

## State fields

| Bytes | Meaning | Absent means |
|---|---|---|
| `10 00` | Night light is on now | off |
| `D0 0A 02` | set by hand ("Turn on now" or off), not by the schedule | the schedule decided |
| `C6 14 <varint>` | when it last changed | |

## Reading it safely

- Check the header and the length; walk the fields by their tags; stop and report "unknown" at an unexpected tag.
- Don't rely on `RegNotifyChangeKeyValue`: Windows writes these without notifications firing. Poll them.
- Sunset and sunrise come from Windows' own location; use them only when the schedule is "Sunset to sunrise".

## How BlinkBuddy uses it

Read-only, in `platform/NightLight.cpp`, read again each second. (Windows changes these values without the
registry's change notifications firing: a watch on the keys, or on the whole store, stayed silent while Night light
was switched on and off.) With **Sync with Windows Night light** on (Schedule page, on
by default):

- While Night light is on, BlinkBuddy's blue light filter rests (daytime and night strength both); brightness, tone
  and effects carry on. Things drawn to match the screen's warmth (mini mode's icon) use Night light's.
- While Night light has a schedule, BlinkBuddy's night schedule runs on its hours (Set hours, or Windows' sunset and
  sunrise) and its strength (as a blue light filter %: (6500 − K) / 46). BlinkBuddy's own schedule settings are kept
  for when it doesn't.
