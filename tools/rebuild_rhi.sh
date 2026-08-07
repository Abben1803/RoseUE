#!/usr/bin/env bash
# Rebuild catalog textures: re-import every item under RHI (nullrhi skipped the
# texture build -> checkered).  Chunks of 50 in fresh processes (releases GPU
# between chunks -> no TDR).  Resumable via a per-gender progress file.  Then
# applies the two-sided/masked material pass.  Editor MUST stay closed.
set -u
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="C:/rose-next-classic/unreal-engine rose/RoseUE/RoseUE.uproject"
T="C:/rose-next-classic/tools"
GLTF="C:/rose-next-classic/unreal-engine rose/RoseUE/SourceAssets/GLTF/AVATAR/MODULAR"
CHUNK=50

reimport_gender() {
  local G=$1                      # FEMALE / MALE
  local dir="$GLTF/$G"
  local prog="$T/_tmp/rebuild_${G}.idx"
  # -printf '%f' = basename without splitting on spaces in the path
  mapfile -t NAMES < <(find "$dir" -maxdepth 1 -name '*.glb' -printf '%f\n' 2>/dev/null | sed 's/\.glb$//' | grep -vx base | sort)
  local total=${#NAMES[@]}
  local start=0; [ -f "$prog" ] && start=$(cat "$prog")
  echo "=== $G: $total items, resuming at $start ($(date +%H:%M:%S)) ==="
  local i=$start
  while [ "$i" -lt "$total" ]; do
    local chunk=("${NAMES[@]:i:CHUNK}")
    local only; only=$(IFS=,; echo "${chunk[*]}")
    ROSE_GENDER="$G" ROSE_NORESKIN=1 ROSE_NOCOMPAT=1 ROSE_ONLY="$only" \
      "$UE" "$PROJ" -ExecutePythonScript="$T/ue5_import_modular.py" \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
      -abslog="$T/import_${G}.log" >/dev/null 2>&1
    if grep -q "\[mod\] done" "$T/import_${G}.log" 2>/dev/null; then
      i=$((i+CHUNK)); echo "$i" > "$prog"
      echo "  $G $i/$total ($(date +%H:%M:%S))"
    else
      echo "  $G chunk at $i did not finish (retrying) ($(date +%H:%M:%S))"
    fi
  done
  echo "=== $G reimport COMPLETE ($(date +%H:%M:%S)) ==="
}

run_py() {  # $1 script $2 log ; env from caller
  "$UE" "$PROJ" -ExecutePythonScript="$T/$1" -unattended -nopause -nosplash \
    -nullrhi -stdout -FullStdOutLogOutput -abslog="$T/$2" >/dev/null 2>&1
}

for G in FEMALE MALE; do
  reimport_gender "$G"
  echo "=== $G two-sided pass ==="
  ROSE_GENDER="$G" run_py ue5_fix_sidedness.py "twoside_${G}.log"
  grep "2side\] done" "$T/twoside_${G}.log" 2>/dev/null | tail -1 | sed 's/.*LogPython: //'
done
echo "=== REBUILD DONE ($(date +%H:%M:%S)) ==="
