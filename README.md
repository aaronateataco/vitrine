# LUDI-NX

A source-available emulation frontend for Nintendo Switch homebrew. Lists your **homebrew
apps**, **installed Switch games**, and **ROMs** in one place, and launches all three.

*Ludi* is Latin for "the games"; `NX` is the Switch's codename.

Written against libnx only — no portlibs, no external dependencies.

## Status

Early. Scanning, filtering and all three launch paths are implemented; the UI is
text-based while the core settles. An icon grid is the next step — the metadata layer
already locates the artwork, it just isn't decoded yet.

**Not yet built or run.** It was written without a devkitPro toolchain available, so
treat the first `make` as part of the work. The pure-logic layers (NRO asset parsing,
INI parsing, extension matching, ROM scanning) are covered by host-side tests that pass
under AddressSanitizer; the `ns` and `applet` paths need real hardware.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` group:

```sh
sudo dkp-pacman -S switch-dev
export DEVKITPRO=/opt/devkitpro
make
```

Produces `ludi-nx.nro`. Copy it to `/switch/ludi-nx/ludi-nx.nro` on your SD card and
start it from hbmenu.

## Controls

| Input | Action |
|---|---|
| D-pad up/down | Move |
| D-pad left/right | Page |
| A | Launch |
| X | Cycle filter (all / homebrew / roms / switch) |
| Y | Rescan |
| + | Exit |

## Configuring systems

On first run LUDI-NX writes a commented starter file to
`/switch/ludi-nx/systems.ini`. One section per platform:

```ini
[Game Boy Advance]
core = sdmc:/switch/ludi-nx/cores/mgba_libretro_libnx.nro
roms = sdmc:/roms/gba
roms = sdmc:/tico/roms/gba
extensions = gba, agb

[Nintendo 64]
core = sdmc:/switch/ludi-nx/cores/mupen64plus_next_libretro_libnx.nro
roms = sdmc:/roms/n64
roms = sdmc:/tico/roms/n64
extensions = n64, z64, v64
args = -L "{core}" --rom "{rom}"
```

`roms` is repeatable — up to six directories per system, so an existing **tico** library
is picked up in place alongside LUDI-NX's own layout. Directories that don't exist are
skipped silently, so listing both costs nothing. tico's exact on-card layout isn't
documented publicly, so adjust the `sdmc:/tico/...` paths if yours differ.

`args` is optional and defaults to `"{core}" "{rom}"`. Press **Y** in the app to reload
the file after editing.

## Cores

LUDI-NX bundles no cores. `tools/fetch-cores.sh` clones and builds them from the public
`ticohq/tico-*` forks on your own machine:

```sh
./tools/fetch-cores.sh --list     # show cores and licenses
./tools/fetch-cores.sh            # clone and build all of them
./tools/fetch-cores.sh --free-only  # skip the non-commercial ones
```

Three cores — **snes9x**, **Genesis Plus GX** and **FBNeo** — carry licenses forbidding
*commercial* use. Building and running them yourself is well within those terms, so they
are built by default; the restriction only matters if you sell or commercially distribute
the result. Everything else is GPL-2.0-or-later, GPL-3.0 or MPL-2.0.

Cores run as **separate NRO processes**, launched via `envSetNextLoad` exactly like any
other homebrew. Nothing is linked into LUDI-NX, so each core keeps its own license and
this frontend stays MIT.

If you redistribute a GPL core NRO you must also offer its complete corresponding
source, including your changes.

## How it works

**Homebrew** — scans `sdmc:/switch` one level deep, following the hbmenu layout. Name
and author come from the NRO's appended asset blob: seek to the `size` field in the NRO
header, read the `ASET` header there, and pull the NACP section. NROs without an asset
blob fall back to their filename.

**Installed games** — `nsListApplicationRecord` paginates the record list, then
`nsGetApplicationControlData` fetches control data and `nsGetApplicationDesiredLanguage`
picks the entry matching the console language. Archived titles (record present, content
removed) fail the control-data call and are dropped, since they would not launch anyway.
Launching goes through `appletRequestLaunchApplication`.

That call is restricted to `AppletType_*Application`, or `AppletType_LibraryApplet` on
[5.0.0+] — covering both title takeover and album takeover on modern firmware. It is
checked at runtime, and the UI degrades to homebrew-only rather than failing cryptically.

**ROMs** — each configured system's directory is walked recursively and matched against
its extension list.

## Layout

```
source/
  entry.h/.c     shared entry model, list container, path helper
  config.h/.c    systems.ini parsing, extension matching, arg templates
  homebrew.c     /switch scanning, NRO asset header parsing
  titles.c       installed title enumeration via ns
  roms.h/.c      ROM directory scanning
  launch.h/.c    all three launch paths, with applet-type gating
  ui.h/.c        console rendering
  main.c         input handling and main loop
tools/
  fetch-cores.sh clone and build cores from source
```

## Legal

LUDI-NX contains no ROMs, BIOS images, system firmware, keys, or emulator cores, and
performs no DRM circumvention.

Homebrew launching involves no third-party copyrighted work. For installed games,
LUDI-NX only passes an application id the console already has installed to the OS — the
system performs all decryption and signature verification itself, exactly as it does from
the HOME menu. Names and icons are read from the user's own console at runtime and are
never redistributed. You are responsible for legally owning anything you point it at.

Not affiliated with Nintendo. Nintendo Switch is a trademark of Nintendo Co., Ltd.

## License

MIT terms plus an exclusion clause denying any licence to the tico project and ticohq —
see [LICENSE](LICENSE). That exclusion is a restriction on who may use the software, so
this is *source-available* rather than open source in the OSI sense.
