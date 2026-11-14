#!/usr/bin/env bash
# uishots.sh: screenshots of the wrapper UI's screens at several UI scales,
# to check a change to the UI's look before it is shown to anyone. No save and
# no level are needed: each screen opens at startup (DES_UI_OPEN) over the
# game's first frames.
#
# Usage: tools/des/uishots.sh <tag> [screen ...]
#   screens: settings (the default; one shot per tab), menu, trophies, mods,
#            players, setup
#   UISHOTS_SCALES    the UI scale setting (ui_scale) per run, in percent
#                     (default "100 150"; the settings row offers 75 to 150)
#   UISHOTS_TABS      settings tabs to shoot (default 7)
#   UISHOTS_SECS      the most a run may take (default 150)
#   DES_EXE           the build (default port/build/des_boot.exe), as desrun.sh
# Shots go to $DES_SCRATCH/uishots/<tag>/<screen>_<scale>[_tabN].png (BMP when
# Python has no PIL). The window is the game's 1280x720, so a run can't try
# other window shapes or DPIs (DES_UI_DPI only counts in a bigger window).
# Each run uses a scratch settings file and data folder, so the player's
# settings, saves and trophies are never read or written.
#
# A run here stops only its own game (des_stop_games on its own pid file),
# never des_kill_own: that also stops test copies staged by other scratch
# folders, which may be another run in progress.
set -uo pipefail
. "$(dirname "$0")/deslib.sh"
cd "$DES_ROOT" || exit 1

TAG="${1:?usage: uishots.sh <tag> [screen ...]}"; shift
SCREENS=("$@"); [ ${#SCREENS[@]} -gt 0 ] || SCREENS=(settings)
SCALES="${UISHOTS_SCALES:-100 150}"
TABS="${UISHOTS_TABS:-7}"
SECS="${UISHOTS_SECS:-150}"
OUT="$DES_SCRATCH/uishots/$TAG"
mkdir -p "$OUT"
w() { cygpath -m "$1" 2>/dev/null || echo "$1"; }

des_env_defaults
EXE="$(des_stage_exe)" || { echo "uishots: could not stage the exe"; exit 1; }
des_window_launch

# bmp_done <file>: the file is a whole BMP (its header's size reached).
bmp_done() {
  [ -f "$1" ] || return 1
  local want have
  want=$(od -An -tu4 -j2 -N4 "$1" 2>/dev/null | tr -d ' ')
  have=$(stat -c %s "$1" 2>/dev/null)
  [ -n "$want" ] && [ -n "$have" ] && [ "$have" -ge "$want" ]
}

# shoot <screen> <scale>: one run, its shots into $OUT.
shoot() {
  local screen="$1" scale="$2" name run shots="" keys="" last f i
  name="${screen}_${scale}"
  run="$DES_SCRATCH/uishots_run/$name"
  rm -rf "$run"; mkdir -p "$run/data" "$run/saves"
  printf 'ui_scale=%s\n' "$scale" > "$run/settings.ini"
  if [ "$screen" = settings ]; then
    for ((i = 0; i < TABS; i++)); do
      f=$((240 + i * 40))
      shots+="${shots:+;}$(w "$run/${name}_tab$((i + 1)).bmp"),$f"
      [ $i -gt 0 ] && keys+="${keys:+,}$((f - 20)):PGDN"
    done
  else
    shots="$(w "$run/$name.bmp"),240"
  fi
  last="${shots##*;}"; last="${last%,*}"
  : > "$DES_PIDFILE"
  ( export DES_UI_OPEN="$screen" DES_UI_NO_ANIM=1 DES_REPORT_NO_OPEN=1 \
           DES_UI_SHOT="$shots" \
           DES_SETTINGS="$(w "$run/settings.ini")" DES_DATA_DIR="$(w "$run/data")" \
           PS3_SAVEDATA_ROOT="$(w "$run/saves")" DES_SKIP_INTRO=1 PS3_SAIL_MOVIES=0
    [ -n "$keys" ] && export DES_UI_KEYS="$keys"
    exec "${DES_LAUNCH[@]}" "$EXE" work/game/EBOOT.ELF > "$run/log.txt" 2>&1 ) &
  for ((i = 0; i < SECS * 2; i++)); do
    bmp_done "$(cygpath -u "$last" 2>/dev/null || echo "$last")" && break
    sleep 0.5
  done
  des_stop_games "$DES_PIDFILE" || echo "uishots: $name: the game did not stop" >&2
  wait 2>/dev/null
  for f in "$run"/*.bmp; do
    [ -f "$f" ] || continue
    if python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
         "$(w "$f")" "$(w "$OUT/$(basename "${f%.bmp}").png")" 2>/dev/null; then :
    else cp "$f" "$OUT/"; fi
  done
  echo "uishots: $name: $(ls "$OUT" | grep -c "^$name") shot(s)"
}

for screen in "${SCREENS[@]}"; do
  for v in $SCALES; do shoot "$screen" "$v"; done
done
echo "uishots: $OUT"
