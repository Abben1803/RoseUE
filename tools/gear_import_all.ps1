# gear_import_all.ps1 — Phase 4 driver: import all 5,510 gear mesh GLBs in
# fresh -nullrhi batches (no GPU/TDR risk; each process resets memory).
# Resumable: re-running skips already-imported meshes.  Waits for the editor to
# be closed before each batch.  Progress: tools\_tmp\gear_import.log
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$LOGDIR = "$TOOLS\_tmp\gear"
$PROGRESS = "$TOOLS\_tmp\gear_import.log"
$BATCH = 900
New-Item -ItemType Directory -Force $LOGDIR | Out-Null

function Log($m) { Add-Content $PROGRESS ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }
function WaitEditorClosed() {
    while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 20 }
}

$GEARDIR = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content\Characters\Gear"
function MeshCount() {
    (Get-ChildItem -Path $GEARDIR -Directory -Filter "mesh_*" -ErrorAction SilentlyContinue | Measure-Object).Count
}

Log "=== GEAR IMPORT START (batch=$BATCH) ==="
for ($i = 1; $i -le 12; $i++) {
    WaitEditorClosed
    $before = MeshCount
    $log = "$LOGDIR\batch_$i.log"
    $env:ROSE_LIMIT = "$BATCH"
    & $UE $PROJ "-ExecutePythonScript=$TOOLS\ue5_import_gear.py" -unattended -nopause `
        -nosplash -nullrhi -stdout -FullStdOutLogOutput -abslog=$log *> $null
    Remove-Item env:ROSE_LIMIT -ErrorAction SilentlyContinue
    $after = MeshCount
    Log "batch $i : $before -> $after mesh dirs (+$($after - $before))"
    if ($after -le $before) { Log "no new meshes imported — done"; break }
}
Log "=== GEAR IMPORT COMPLETE ($(MeshCount) meshes) ==="
