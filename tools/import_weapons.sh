#!/usr/bin/env bash
# Import the full weapon catalog (weapon_*.glb) under RHI in fresh-process chunks
# of 50 (textures build; GPU released between chunks -> no TDR).  Resumable via a
# progress file.  ROSE_NORESKIN keeps Interchange's textured M_GLTF; ROSE_NOCOMPAT
# skips compat marking (the runtime merge accepts non-compat parts); SKIP_EXISTING
# leaves already-imported weapons (e.g. weapon_1..5) alone.  Editor MUST stay closed.
#
# Usage:  bash import_weapons.sh FEMALE      (or MALE)
set -u
G="${1:-FEMALE}"
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="C:/rose-next-classic/unreal-engine rose/RoseUE/RoseUE.uproject"
T="C:/rose-next-classic/tools"
GLTF="C:/rose-next-classic/unreal-engine rose/RoseUE/SourceAssets/GLTF/AVATAR/MODULAR"
CHUNK=50

dir="$GLTF/$G"
prog="$T/_tmp/weapons_${G}.idx"
mkdir -p "$T/_tmp"
mapfile -t NAMES < <(find "$dir" -maxdepth 1 -name 'weapon_*.glb' -printf '%f\n' 2>/dev/null | sed 's/\.glb$//' | sort)
total=${#NAMES[@]}
start=0; [ -f "$prog" ] && start=$(cat "$prog")
echo "=== $G weapons: $total GLBs, resuming at $start ($(date +%H:%M:%S)) ==="
i=$start
while [ "$i" -lt "$total" ]; do
  chunk=("${NAMES[@]:i:CHUNK}")
  only=$(IFS=,; echo "${chunk[*]}")
  ROSE_GENDER="$G" ROSE_NORESKIN=1 ROSE_NOCOMPAT=1 ROSE_SKIP_EXISTING=1 ROSE_ONLY="$only" \
    "$UE" "$PROJ" -ExecutePythonScript="$T/ue5_import_modular.py" \
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
    -abslog="$T/_tmp/import_weapons_${G}.log" >/dev/null 2>&1
  if grep -q "\[mod\] done" "$T/_tmp/import_weapons_${G}.log" 2>/dev/null; then
    i=$((i+CHUNK)); echo "$i" > "$prog"
    echo "  $G $i/$total imported ($(date +%H:%M:%S))"
  else
    echo "  $G chunk at $i did not finish (retrying) ($(date +%H:%M:%S))"
  fi
done
echo "=== $G weapons import COMPLETE ($(date +%H:%M:%S)) ==="
