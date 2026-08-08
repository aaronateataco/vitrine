# Diagnostics drop folder

Copy the contents of `sdmc:/switch/vitrine/` from your SD card into this folder
after a hardware session. Everything here is generated on-device.

| File | What it tells us |
|---|---|
| `vitrine.log` | Timestamped session log, flushed on every line so a crash still leaves a trail. Applet mode, scan results, per-system core resolution, every launch attempt and its result code. |
| `diagnostics.txt` | Full state report: firmware, applet type, preferences, shelf composition, every configured system with which core candidate was actually used, all overrides, and the identity of `sdmc:/hbmenu.nro`. |
| `screenshots/*.png` | Framebuffer grabs. |
| `config.json` | Preferences and per-item overrides. |
| `systems.ini` | Core and ROM paths. |

## Producing them

- **Left stick click** — screenshot, any time.
- **Settings (`-`) → Save screenshot + report** — screenshot plus the full report.

The log is written from startup, so it exists even if the app refuses to run or
crashes before you reach the library.

## The `hbmenu.nro` question

`diagnostics.txt` reads the NACP title out of `sdmc:/hbmenu.nro` and prints it.
That file is where hbloader sends the console when a program exits without
naming a successor — so if it turns out to be tico, that is why exiting a game
returns you there, and no change on the launcher side can override it.
