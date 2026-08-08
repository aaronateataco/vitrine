# VITRINE

A visuals-first game launcher for Nintendo Switch homebrew. Your **installed games**,
**homebrew apps** and **ROMs** in one place, grouped by platform and launched from a
single controller-driven interface.

A *vitrine* is a glass case for showing off a collection — which is what this is.

## Collaboration

I would love for an iisu developer to contact me at **aaron@aaronworld.uk** to collaborate
on making this an official/unofficial client that supports iisu accounts for the Nintendo
Switch.

## Requires Application Mode

VITRINE refuses to start in applet mode and explains why on screen.

hbloader grants applet mode roughly **56 MB** of heap, against gigabytes under title
takeover. Emulator cores and high-resolution artwork do not fit in 56 MB, and a ROM
launched from applet mode dies with *"The software was closed because an error occurred"*.

**To launch correctly:** in hbmenu, **hold R while starting any installed game**, then run
VITRINE from the menu that appears.

## Status

Working on hardware: scanning, platform grouping, and all three launch paths. The UI is
SDL2 with the console's own system font.

Verified by CI on every push — host tests under AddressSanitizer, plus a cross build that
asserts the output is a well-formed NRO. Visual layout is the part CI cannot check.

## Controls

| Input | Action |
|---|---|
| D-pad ←/→ | Move within a shelf |
| D-pad ↑/↓ | Change shelf |
| L / R | Page within a shelf |
| A | Launch |
| X | Hide / unhide the selected item |
| ZL | Move homebrew into Installed Games (and back) |
| ZR | Show hidden items, so they can be unhidden |
| Y | Rescan |
| Left stick click | Screenshot |
| Right stick click | Choose a cover for this game |
| B | Open the Game Room for this shelf |
| - | Settings |
| + | Exit |

Settings covers **Appearance** (theme, cover shape, cover size), **Covers** (SteamGridDB
key, download), **Library** (show hidden, rescan, unhide everything) and **Shelves** — a
per-platform visibility toggle, so Homebrew or any console can be switched off entirely.

### Layouts

**Shelves** (default) stacks one horizontal row per platform, so the whole library is
visible at once. **Console** follows the shape of the Switch 2 HOME menu: an avatar mark
top-left, a horizontal carousel of games through the middle, the selected title beneath
it, and a slim dock along the bottom. The console's dock holds system apps; here it holds
your platforms, which is the closest useful equivalent and keeps a multi-platform library
reachable from one row.

Controls are identical in both — left/right moves through games, up/down changes platform.

Proportions were estimated from written descriptions of the console's menu rather than
measured from it, so this reads as the same shape rather than a pixel replica.

Four themes: **Vitrine Dark**, **Switch 2 Dark**, **Switch 2 Light** and **Daylight**.
The Switch 2 pair is monochrome because that is what the console ships — its themes are
only Basic Dark and Basic Light, with no accent colour. Tiles are drawn with rounded
corners to match its icon treatment. Colour values only; no Nintendo assets are used.

Cover size has three steps (Standard / Large / Extra large) in both square and poster
shapes.

## Cover art

VITRINE can pull artwork from [SteamGridDB](https://www.steamgriddb.com). It ships no
artwork and bundles no key.

1. Get a free API key at `steamgriddb.com/profile/preferences/api`.
2. **Settings → SteamGridDB API key** opens the system keyboard.
3. **Settings → Download covers for this shelf** fetches artwork for everything on the
   shelf you were browsing.

Covers land in `sdmc:/switch/vitrine/covers/`, named by a hash of the entry's identity so
they survive rescans. Already-cached entries are skipped, so re-running is cheap. A
downloaded cover takes precedence over the console's own icon.

**Cover shape decides what is requested** — Poster 2:3 fetches 600x900 grids, Square 1:1
fetches icons. Covers uploaded by `sodasoba` are preferred over all others; remaining
candidates are ordered by SteamGridDB's own score.

**Choosing a specific cover:** highlight a game and click the **right stick**. That lists
every candidate with its uploader and score, previews the highlighted one, and **A** pins
your choice. A pinned cover is recorded in `config.json` and survives re-fetching, so
bulk downloads will not overwrite it. Previews are downloaded only for the highlighted
row, so opening the list stays fast.

HTTPS needs no CA bundle: devkitPro builds curl against the console's own SSL service.

The footer shows only **A play** and **- settings**; the full reference lives in the
Settings overlay, because ten bindings do not fit along a 720p footer.

## How the library is organised

One vertically scrolling page of horizontal shelves, one per platform. Libraries here are
very unevenly sized — three GBA ROMs against forty homebrew — and shelves handle that far
better than a flat grid, which does not scale, or a drill-down menu, which adds a step
that is absurd for a two-game platform. Grouping and browsing become the same gesture.

Shelf order is deliberate: installed games, then homebrew, then platforms **in the order
`systems.ini` declares them**, because that ordering is yours. Empty platforms are
dropped, and each shelf remembers its cursor across rescans.

## Configuration

Two files under `sdmc:/switch/vitrine/`, both created on first run.

**`systems.ini`** — one section per platform:

```ini
[Game Boy Advance]
core = sdmc:/switch/vitrine/cores/tico-mgba.nro
core = sdmc:/tico/cores/tico-mgba.nro
roms = sdmc:/roms/gba
roms = sdmc:/tico/roms/gba
extensions = gba, agb
args = "{core}" "{rom}"
```

A section may instead use `nro = <directory>` to claim a folder of standalone NROs, which
then get their own shelf rather than sitting in Homebrew:

```ini
[Viridite]
nro = sdmc:/switch/Viridite Games
```

Those launch directly, with no core involved. The generic `/switch` sweep skips anything
already claimed this way, so nothing appears twice.

`core` and `roms` are both repeatable — cores in preference order, ROM directories all
scanned — so an existing **tico** library is picked up in place without depending on it. Missing directories are skipped silently. `args` defaults to `"{core}" "{rom}"`;
set it per system if a core wants a different command line. Press **Y** to reload.

`examples/systems-tico.ini` covers all twenty systems a stock tico install provides.

**`config.json`** — your per-item decisions, written automatically:

```json
{
  "version": 1,
  "entries": {
    "sdmc:/switch/dbi/DBI.nro": { "hidden": true },
    "sdmc:/switch/RetroArch/retroarch.nro": { "promote": true },
    "title:0100000000010000": { "hidden": true }
  }
}
```

Homebrew and ROMs are keyed by path; installed titles by application id, since their
storage path is an implementation detail. Entries back at their defaults are dropped, so
the file only ever records what you actually changed.

## Cores, and not depending on tico

VITRINE bundles no cores. `core` is **repeatable, in preference order** — the first
candidate that exists on disk is used:

```ini
core = sdmc:/switch/vitrine/cores/tico-mgba.nro   # your own build, preferred
core = sdmc:/tico/cores/tico-mgba.nro             # a tico install, fallback
```

This is what keeps VITRINE independent. It reads tico's ROM folders and can borrow its
cores, but needs neither.

### Exiting a game returns me to tico

**The cores are the cause.** tico's core NROs chain-load back to `tico.nro` when they
exit, so borrowing them from `/tico/cores/` inherits that. Nothing VITRINE does can
override where another program decides to go next.

This was confirmed by diagnostics: `sdmc:/hbmenu.nro` is not tico, so hbloader's default
return target is not to blame — the cores are choosing tico explicitly.

The fix is to use cores that return to whoever launched them. `tools/get-cores.sh`
downloads those from libretro's official buildbot; drop them in
`sdmc:/switch/vitrine/cores/` and put that path first in each system's `core =` list.
Run **Settings → Save screenshot + report** and check `diagnostics.txt` to see which
core candidate is actually being used.

**Prebuilt cores, the easy route.** libretro's official buildbot publishes Switch core
NROs, which return to whatever launched them:

```sh
./tools/get-cores.sh --list   # system -> core mapping
./tools/get-cores.sh          # download into ./cores/
```

Copy those to `sdmc:/switch/vitrine/cores/`. It covers 14 systems; GameCube, Wii, 3DS and
Saturn are not on the buildbot and still need a tico install.

To build from source instead, `tools/fetch-cores.sh` clones and builds from the public
`ticohq/tico-*` forks:

```sh
./tools/fetch-cores.sh --list       # cores and their licenses
./tools/fetch-cores.sh              # build all
./tools/fetch-cores.sh --free-only  # skip the non-commercial ones
```

**snes9x**, **Genesis Plus GX** and **FBNeo** forbid *commercial* use. Building and running
them yourself is well within those terms; the restriction only matters if you sell or
commercially distribute the result. Everything else is GPL-2.0-or-later, GPL-3.0 or
MPL-2.0.

Cores run as **separate NRO processes** via `envSetNextLoad`. Nothing is linked into
VITRINE, so each core keeps its own license and this frontend stays independent. If you
redistribute a GPL core NRO you must also offer its complete corresponding source.

## Game Rooms (in progress)

Pressing **B** on a shelf opens a 3D room. The GLES foundation is in: a lit,
orbitable placeholder solid rendered into the same context SDL already owns, with
D-pad orbit and L/R zoom.

`franchises.json` (written to `sdmc:/switch/vitrine/` on first run) maps a shelf to its
room:

```json
{
  "version": 1,
  "rooms": [
    {
      "match": "Game Boy Advance",
      "title": "GBA Room",
      "model": "sdmc:/switch/vitrine/models/gba-room.glb",
      "source": "https://sketchfab.com/3d-models/example",
      "credit": "Model by Example Author, CC-BY 4.0",
      "camera": { "yaw": 0.6, "pitch": 0.3, "distance": 6.0, "target": [0, 1, 0] }
    }
  ]
}
```

`match` is the shelf name; the camera framing is applied when the room opens, and
`credit`/`source` are displayed inside it. Only `match` is required — everything else
falls back to defaults.

**VITRINE re-hosts no 3D models.** Most published models are not licensed for
redistribution, and routing around a download restriction would not change that. So the
file carries camera coordinates and a **link**, and `model` points at a file **you** put
on your SD card. Model loading itself is the next step; the framing, attribution and
lookup are already in.

## Trophy Room

**Settings → Open Trophy Room** renders your recent RetroAchievements unlocks as 3D
medals: the achievement badge is mapped onto both faces of a coin, with a metal rim and a
procedurally woven ribbon above it. Left/right moves between medals, L/R zooms, B exits.

Needs your RetroAchievements username and **web API key** (from
`retroachievements.org/controlpanel.php`), both set in Settings via the system keyboard.
Badges are cached under `sdmc:/switch/vitrine/badges/`.

Lighting is metallic-looking Blinn-Phong with a specular term, not true PBR — there is no
environment map to sample on a console launcher, and one would not buy much at this size.
A medal whose badge failed to download still renders as a plain metal coin.

## Building

### With a container (recommended on Fedora and any non-Debian distro)

```sh
sudo dnf install -y podman        # or: apt install podman / docker
podman run --rm -v "$PWD":/project:Z -w /project docker.io/devkitpro/devkita64 make
```

Two things about that command are load-bearing:

- **`:Z`** relabels the bind mount for SELinux; without it the container gets permission
  denied on Fedora.
- **The mount point must not be `/build`.** devkitPro's Makefile distinguishes its outer
  and inner invocations with `ifneq ($(BUILD),$(notdir $(CURDIR)))`, and `BUILD` is
  `build`. Working in a directory named `build` makes that test match, so `OUTPUT` is
  never set and the build fails with `No rule to make target '.nacp'`.

devkitPro's current installer release ships only a macOS package, so a native install on
Fedora means building their pacman fork yourself — the container is the easier road.

Either route produces `vitrine.nro`. Copy it to `/switch/vitrine/vitrine.nro`.

### Tests

```sh
make -C tests          # four suites under AddressSanitizer + UBSan
make -C tests compile  # portable sources at -O2 -Wall -Wextra -Werror
```

The `compile` target matters: `-O2` is what surfaces GCC's format-truncation and
array-bounds diagnostics, which a syntax-only check silently misses. It has caught real
bugs more than once.

### Releases

Pushing a `v*` tag publishes a **draft** release with an SD-card-ready archive:

```sh
git tag v0.1.0 && git push origin v0.1.0
```

## Layout

```
source/
  entry.h/.c      shared entry model, list container, path helper
  config.h/.c     systems.ini parsing, extension matching, arg templates
  overrides.h/.c  config.json: hiding and re-tagging
  shelves.h/.c    platform grouping
  homebrew.c      /switch scanning, NRO asset header parsing
  titles.c        installed title enumeration via ns
  roms.h/.c       ROM directory scanning
  launch.h/.c     all launch paths, with applet-type gating
  render.h/.c     SDL2, system font, text cache
  icons.h/.c      lazy icon decoding and texture cache
  ui.h/.c         shelves, footer, settings overlay, mode gate
  main.c          input handling and main loop
  vendor/         cJSON (MIT), vendored
```

## Legal

VITRINE contains no ROMs, BIOS images, system firmware, keys, or emulator cores, and
performs no DRM circumvention.

Homebrew launching involves no third-party copyrighted work. For installed games, VITRINE
passes an application id the console already has installed to the OS — the system performs
all decryption and signature verification itself, exactly as from the HOME menu. Names and
icons are read from your own console at runtime and are never redistributed. You are
responsible for legally owning anything you point it at.

Bundled third-party code: [cJSON](https://github.com/DaveGamble/cJSON) by Dave Gamble and
contributors, MIT licensed, in `source/vendor/`.

Not affiliated with Nintendo, tico, or iisu. Nintendo Switch is a trademark of Nintendo
Co., Ltd.

## License

MIT terms plus an exclusion clause denying any licence to the tico project and ticohq —
see [LICENSE](LICENSE). That exclusion restricts who may use the software, so this is
*source-available* rather than open source in the OSI sense.
