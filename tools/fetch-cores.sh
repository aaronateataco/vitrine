#!/usr/bin/env bash
#
# Clones and builds libretro cores from the ticohq forks for Switch.
#
# LUDI-NX ships no cores. This script builds them on your machine from public
# source, which is what those licenses allow. It does not download binaries and
# it does not touch ROMs, keys, or firmware.
#
# UNTESTED: written without a devkitPro toolchain available. Expect to fix
# per-core build flags. Treat the first run as work, not a formality.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT}/cores/src"
OUT_DIR="${ROOT}/cores/build"
ORG="https://github.com/ticohq"

# repo:make-target:license:class
#
# "noncommercial" marks cores whose licenses forbid *commercial* use. Building
# and using them yourself is squarely within those terms, so they are built by
# default; the restriction only bites if you sell or commercially distribute the
# result. Use --free-only to skip them. Verified against each repo's license file.
CORES=(
  "tico-mgba:mgba:MPL-2.0:free"
  "tico-fceumm:fceumm:GPL-2.0-or-later:free"
  "tico-gambatte:gambatte:GPL-2.0-or-later:free"
  "tico-mupen64plus:mupen64plus_next:GPL-2.0-or-later:free"
  "tico-flycast:flycast:GPL-2.0-or-later:free"
  "tico-azahar:azahar:GPL-2.0-or-later:free"
  "tico-yabasanshiro:yabasanshiro:GPL-2.0-or-later:free"
  "tico-tresdeesse:tresdeesse:GPL-2.0-or-later:free"
  "tico-melonds:melonds:GPL-3.0:free"
  "tico-duckstation:duckstation:GPL-3.0:free"
  "tico-swanstation:swanstation:GPL-3.0:free"
  "tico-ppsspp:ppsspp:GPL-2.0:free"
  "tico-dolphin:dolphin:GPL-2.0:free"
  "tico-snes9x:snes9x:Snes9x-nc:noncommercial"
  "tico-genesisplusgx:genesis_plus_gx:GPX-nc:noncommercial"
  "tico-fbneo:fbneo:FBNeo-nc:noncommercial"
)

FREE_ONLY=0
LIST_ONLY=0

for arg in "$@"; do
  case "$arg" in
    --free-only) FREE_ONLY=1 ;;
    --list)      LIST_ONLY=1 ;;
    -h|--help)
      echo "usage: $0 [--list] [--free-only]"
      exit 0 ;;
    *)
      echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [[ "$LIST_ONLY" == 1 ]]; then
  printf '%-24s %-22s %s\n' REPO LICENSE STATUS
  for entry in "${CORES[@]}"; do
    IFS=: read -r repo _ license status <<< "$entry"
    printf '%-24s %-22s %s\n' "$repo" "$license" "$status"
  done
  exit 0
fi

if [[ -z "${DEVKITPRO:-}" ]]; then
  echo "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO." >&2
  exit 1
fi

mkdir -p "$SRC_DIR" "$OUT_DIR"

built=() skipped=() failed=()

for entry in "${CORES[@]}"; do
  IFS=: read -r repo target license status <<< "$entry"

  if [[ "$status" == "noncommercial" && "$FREE_ONLY" == 1 ]]; then
    echo "skip  $repo ($license, --free-only requested)"
    skipped+=("$repo")
    continue
  fi

  echo "=== $repo ($license) ==="
  if [[ "$status" == "noncommercial" ]]; then
    echo "  note: personal use is fine; do not sell or commercially distribute this core"
  fi

  if [[ -d "$SRC_DIR/$repo/.git" ]]; then
    git -C "$SRC_DIR/$repo" pull --ff-only || true
  else
    git clone --depth 1 --recursive "$ORG/$repo.git" "$SRC_DIR/$repo"
  fi

  # Cores build as static libretro libraries for libnx. Makefile layout varies
  # between cores, so try the common locations before giving up.
  makefile=""
  for candidate in Makefile.libretro Makefile libretro/Makefile; do
    [[ -f "$SRC_DIR/$repo/$candidate" ]] && { makefile="$candidate"; break; }
  done

  if [[ -z "$makefile" ]]; then
    echo "  no recognised Makefile, skipping" >&2
    failed+=("$repo")
    continue
  fi

  if make -C "$SRC_DIR/$repo" -f "$makefile" platform=libnx -j"$(nproc)"; then
    find "$SRC_DIR/$repo" -maxdepth 2 -name "*_libretro_libnx.a" -exec cp {} "$OUT_DIR/" \;
    built+=("$repo")
  else
    echo "  build failed" >&2
    failed+=("$repo")
  fi
done

echo
echo "built:   ${#built[@]}  ${built[*]:-}"
echo "skipped: ${#skipped[@]}  ${skipped[*]:-}"
echo "failed:  ${#failed[@]}  ${failed[*]:-}"
echo
cat <<'EOF'
Next step, and the part that is not automated:

A core .a is not runnable on its own. A *_libretro_libnx.nro is RetroArch's libnx
frontend statically linked against one core, so turning these archives into core
NROs means building RetroArch's libnx target once per core:

    git clone https://github.com/libretro/RetroArch
    make -C RetroArch -f Makefile.libnx LIBRETRO=<core-name>

Point systems.ini at the resulting NROs. If you redistribute any GPL core NRO you
must also offer its complete corresponding source, including your changes.
EOF
