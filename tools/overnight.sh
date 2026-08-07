#!/usr/bin/env bash
# Overnight: finish the Male import, fix hair (masked+2sided) and faces
# (opaque+2sided, fixes eyes) for both genders, then audit textures.
# Editor MUST stay closed.  Everything is -nullrhi (no GPU TDR) and resumable.
set -u
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="C:/rose-next-classic/unreal-engine rose/RoseUE/RoseUE.uproject"
T="C:/rose-next-classic/tools"

run_py() {  # $1=script $2=log ; env vars inherited from caller line
  "$UE" "$PROJ" -ExecutePythonScript="$T/$1" \
    -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput \
    -abslog="$T/$2" >/dev/null 2>&1
}

echo "=== MALE import ($(date +%H:%M:%S)) ==="
for i in $(seq 1 4); do
  ROSE_GENDER=MALE ROSE_NORESKIN=1 ROSE_NOCOMPAT=1 ROSE_SKIP_EXISTING=1 \
    run_py ue5_import_modular.py import_MALE.log
  n=$(grep -oE "import phase: [0-9]+ imported" "$T/import_MALE.log" 2>/dev/null | grep -oE "[0-9]+" | head -1)
  echo "  MALE pass $i: imported ${n:-CRASH} ($(date +%H:%M:%S))"
  [ "${n:-x}" = "0" ] && break
  [ "${n:-99}" -le 2 ] 2>/dev/null && break   # only stragglers/invalid left
done

echo "=== hair+face -> masked+twosided (both genders) ==="
run_py ue5_fix_alpha.py fixalpha_night.log
grep -c "Masked+TwoSided" "$T/fixalpha_night.log" 2>/dev/null | xargs echo "  hair/face sections:"

echo "=== faces -> opaque+twosided (un-mask eyes, both genders) ==="
run_py ue5_fix_face.py fixface_night.log
grep "face\] done" "$T/fixface_night.log" 2>/dev/null | tail -1

echo "=== audit MALE textures ==="
ROSE_GENDER=Male run_py ue5_audit_textures.py audit_male.log
grep "\[audit\]" "$T/audit_male.log" 2>/dev/null | grep "LogPython" | sed 's/.*LogPython: //'

echo "=== OVERNIGHT DONE ($(date +%H:%M:%S)) ==="
