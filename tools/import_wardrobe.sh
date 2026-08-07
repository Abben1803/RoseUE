#!/usr/bin/env bash
# Import the full wardrobe for both genders, resumable + auto-retry (survives GPU
# TDR).  Each run skips already-imported items, so retries continue where it left
# off.  Editor must stay CLOSED.
set -u
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="C:/rose-next-classic/unreal-engine rose/RoseUE/RoseUE.uproject"
SCRIPT="C:/rose-next-classic/tools/ue5_import_modular.py"

run_gender() {
  local G=$1
  local LOG="C:/rose-next-classic/tools/import_${G}.log"
  for attempt in $(seq 1 12); do
    echo "=== $G attempt $attempt ($(date +%H:%M:%S)) ==="
    # -nullrhi: no GPU work during import → no TDR crashes.  Textures still build
    # (CPU compressor); material shaders compile later when the editor opens.
    ROSE_GENDER="$G" ROSE_NORESKIN=1 ROSE_NOCOMPAT=1 ROSE_SKIP_EXISTING=1 \
      "$UE" "$PROJ" -ExecutePythonScript="$SCRIPT" \
      -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput -abslog="$LOG" >/dev/null 2>&1
    local n
    n=$(grep -oE "import phase: [0-9]+ imported" "$LOG" 2>/dev/null | grep -oE "[0-9]+" | head -1)
    echo "    -> imported this pass: ${n:-CRASH/none}"
    if [ "${n:-x}" = "0" ]; then echo "=== $G COMPLETE ==="; break; fi
  done
}

run_gender FEMALE
run_gender MALE
echo "=== ALL DONE ($(date +%H:%M:%S)) ==="
