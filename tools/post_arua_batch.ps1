# post_arua_batch.ps1 — runs automatically AFTER reimport_arua_maps.ps1 ends:
#   1. bHasMetallicRoughness=OFF on EVERY M_GLTF material instance in the
#      project (/Game root: maps + characters + monsters + weapons + NPCs)
#   2. avatar-part materials 1:1 with their ZSC flags (ue5_apply_zsc.py)
#   3. compile the game DLL (quest teleport + persistence + quest-item fixes
#      were linked out while the batch held the DLL)
# Progress: tools\_tmp\post_arua.log
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BUILD = "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$LOGDIR = "$TOOLS\_tmp\arua"
$OUT = "$TOOLS\_tmp\post_arua.log"

function Log($m) { Add-Content -Path $OUT -Value ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }

Log "waiting for the Arua reimport batch to finish..."
# batch's last step writes metal_all.log; also require every UE process gone
while (-not (Test-Path "$LOGDIR\metal_all.log") -or (Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue) -or (Get-Process UnrealEditor -ErrorAction SilentlyContinue)) {
    Start-Sleep -Seconds 60
}
Log "batch done - starting global material passes"

function RunUE($script, $log, $envs) {
    foreach ($k in $envs.Keys) { Set-Item -Path "env:$k" -Value $envs[$k] }
    & $UE $PROJ -nullrhi "-ExecutePythonScript=$script" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput "-abslog=$log" *> $null
    foreach ($k in $envs.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}

# 1 - metallic OFF project-wide
RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$TOOLS\_tmp\metal_global.log" @{ ROSE_ROOT = "/Game" }
Log "global metallic pass done (see _tmp\metal_global.log)"

# 2 - avatar parts 1:1 with ZSC
RunUE "$TOOLS\ue5_apply_zsc.py" "$TOOLS\_tmp\zsc_avatar.log" @{}
Log "avatar ZSC pass done (see _tmp\zsc_avatar.log)"

# 3 - compile the game DLL
& $BUILD RoseUEEditor Win64 Development "-project=$PROJ" -waitmutex *> "$TOOLS\_tmp\post_build.log"
$ok = Select-String -Path "$TOOLS\_tmp\post_build.log" -Pattern "Result: Succeeded" -Quiet
Log "DLL compile: $(if ($ok) { 'SUCCEEDED' } else { 'FAILED (see _tmp\post_build.log)' })"
Log "=== POST-BATCH COMPLETE ==="
