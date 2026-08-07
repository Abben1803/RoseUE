#!/usr/bin/env bash
# Re-import the back items (geometry changed: now pinned to dummy 3) under RHI in
# 50-item chunks, both genders, then mask them two-sided.  Editor MUST be closed.
set -u
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="C:/rose-next-classic/unreal-engine rose/RoseUE/RoseUE.uproject"
T="C:/rose-next-classic/tools"
GLTF="C:/rose-next-classic/unreal-engine rose/RoseUE/SourceAssets/GLTF/AVATAR/MODULAR"
CHUNK=50

for G in FEMALE MALE; do
  mapfile -t NAMES < <(find "$GLTF/$G" -maxdepth 1 -name 'back_*.glb' -printf '%f\n' 2>/dev/null | sed 's/\.glb$//' | sort)
  total=${#NAMES[@]}
  echo "=== $G back: $total items ($(date +%H:%M:%S)) ==="
  i=0
  while [ "$i" -lt "$total" ]; do
    chunk=("${NAMES[@]:i:CHUNK}"); only=$(IFS=,; echo "${chunk[*]}")
    ROSE_GENDER="$G" ROSE_NORESKIN=1 ROSE_NOCOMPAT=1 ROSE_ONLY="$only" \
      "$UE" "$PROJ" -ExecutePythonScript="$T/ue5_import_modular.py" \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
      -abslog="$T/backimport_${G}.log" >/dev/null 2>&1
    if grep -q "\[mod\] done" "$T/backimport_${G}.log" 2>/dev/null; then
      i=$((i+CHUNK)); echo "  $G back $i/$total ($(date +%H:%M:%S))"
    else
      echo "  $G back chunk at $i did not finish (retry)"
    fi
  done
done

echo "=== mask back items two-sided ==="
ROSE_SLOT=back "$UE" "$PROJ" -ExecutePythonScript="$T/ue5_fix_sidedness.py" \
  -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput \
  -abslog="$T/twoside_back.log" >/dev/null 2>&1
grep "2side\] done" "$T/twoside_back.log" 2>/dev/null | sed 's/.*LogPython: //'
echo "=== BACK REBUILD DONE ($(date +%H:%M:%S)) ==="
