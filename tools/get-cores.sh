#!/usr/bin/env bash
#
# Downloads prebuilt libretro core NROs from libretro's official buildbot into
# a folder you copy to sdmc:/switch/vitrine/cores/.
#
# Why bother when a tico install already has cores: tico's core NROs chain-load
# back to tico.nro when they exit, so borrowing them means "exit game" returns
# you to tico rather than VITRINE. These official builds return to the launcher
# that started them. Nothing in VITRINE can override where another binary
# decides to go next - the only fix is different cores.
#
# Cores are GPL/MPL software distributed by the libretro project. This script
# downloads them; it does not bundle them, and it touches no ROMs or BIOS files.

set -euo pipefail

BASE="https://buildbot.libretro.com/nightly/nintendo/switch/libnx/latest"
OUT="${1:-cores}"

# system-label:buildbot-core-name
CORES=(
  "Nintendo Entertainment System:fceumm"
  "Super Nintendo:snes9x"
  "Nintendo 64:mupen64plus_next"
  "Game Boy:gambatte"
  "Game Boy Color:gambatte"
  "Game Boy Advance:mgba"
  "Nintendo DS:melonds"
  "Master System:genesis_plus_gx"
  "Game Gear:genesis_plus_gx"
  "Genesis:genesis_plus_gx"
  "Sega CD:genesis_plus_gx"
  "Saturn:yabause"
  "Dreamcast:flycast"
  "Naomi:flycast"
  "Atomiswave:flycast"
  "PlayStation:pcsx_rearmed"
  "PSP:ppsspp"
  "FinalBurn Neo:fbneo"
)

if [[ "${1:-}" == "--list" ]]; then
  printf '%-34s %s\n' SYSTEM CORE
  for entry in "${CORES[@]}"; do
    printf '%-34s %s\n' "${entry%%:*}" "${entry##*:}_libretro_libnx.nro"
  done
  exit 0
fi

command -v unzip >/dev/null || { echo "unzip is required" >&2; exit 1; }

mkdir -p "$OUT"
declare -A seen=()
ok=0 failed=0

for entry in "${CORES[@]}"; do
  core="${entry##*:}"

  # Several systems share a core; fetch each only once.
  [[ -n "${seen[$core]:-}" ]] && continue
  seen[$core]=1

  nro="${core}_libretro_libnx.nro"
  if [[ -f "$OUT/$nro" ]]; then
    echo "have  $nro"
    ok=$((ok + 1))
    continue
  fi

  echo "get   $nro"
  if curl -fsSL "$BASE/${nro}.zip" -o "$OUT/${nro}.zip" \
     && unzip -qo "$OUT/${nro}.zip" -d "$OUT"; then
    rm -f "$OUT/${nro}.zip"
    ok=$((ok + 1))
  else
    echo "      failed" >&2
    rm -f "$OUT/${nro}.zip"
    failed=$((failed + 1))
  fi
done

echo
echo "downloaded $ok core(s), $failed failed, into $OUT/"
echo
cat <<EOF
Next:
  1. Copy $OUT/*.nro to sdmc:/switch/vitrine/cores/ on your SD card.
  2. Edit sdmc:/switch/vitrine/systems.ini so each system's first "core =" line
     points at the matching NRO there. VITRINE uses the first candidate that
     exists, so a tico path listed after it stays as a fallback.
  3. Press Y in VITRINE to reload.

Exiting a game should then return to VITRINE instead of tico.
EOF
