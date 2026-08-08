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
| + | Exit |

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
core = sdmc:/tico/cores/tico-mgba.nro
roms = sdmc:/roms/gba
roms = sdmc:/tico/roms/gba
extensions = gba, agb
args = "{core}" "{rom}"
```

`roms` is repeatable (up to six directories), so an existing **tico** library is picked up
in place. Missing directories are skipped silently. `args` defaults to `"{core}" "{rom}"`;
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

## Cores

VITRINE bundles no cores. A stock tico install already ships them prebuilt as standalone
NROs in `/tico/cores/`, and VITRINE points at those directly. To build your own,
`tools/fetch-cores.sh` clones and builds from the public `ticohq/tico-*` forks:

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
  ui.h/.c         shelves, footer, mode gate
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
