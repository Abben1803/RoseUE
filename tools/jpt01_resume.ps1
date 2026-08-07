# jpt01_resume.ps1 — resume jpt01_rebuild_and_fix.ps1 from step 5.
# Steps 1-4 (Arua export, delete, scene import, mob spawners) already completed;
# JPT01 is reimported on disk.  This runs the remainder: portals -> material
# refit -> lighting -> NPCs -> global metallic/masked/collision fixes.
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$ARUA = "C:\rose-next-classic\Arua_Extracted\3DDATA"
$LOGDIR = "$TOOLS\_tmp\jpt01"
$PROGRESS = "$TOOLS\_tmp\jpt01_rebuild.log"

function Log($m) { Add-Content $PROGRESS ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }
function WaitEditorClosed() {
    $warned = $false
    while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
        if (-not $warned) { Log "PAUSED: close UnrealEditor to continue"; $warned = $true }
        Start-Sleep -Seconds 20
    }
    if ($warned) { Log "RESUMED" }
}
function RunUE($script, $log, $envs, $extra) {
    WaitEditorClosed
    foreach ($k in $envs.Keys) { Set-Item -Path "env:$k" -Value $envs[$k] }
    $a = @($PROJ) + $extra + @("-ExecutePythonScript=$script", "-unattended", "-nopause",
                               "-nosplash", "-stdout", "-FullStdOutLogOutput", "-abslog=$log")
    & $UE @a *> $null
    foreach ($k in $envs.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}

Log "=== JPT01 RESUME (from step 5) ==="

RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals.log" @{ ROSE_ZONE = "JPT01" } @("-nullrhi")
Log "JPT01 portals placed"

RunUE "$TOOLS\ue5_refit_map_mats.py" "$LOGDIR\refit.log" `
    @{ ROSE_ZONE = "JPT01"; ROSE_FORCE_SCENE = "1"; ROSE_ASSET_ROOT = $ARUA } @("-nullrhi")
Log "JPT01 materials refit"

RunUE "$TOOLS\ue5_fix_lighting.py" "$LOGDIR\light.log" `
    @{ ROSE_ZONE = "JPT01"; ROSE_SUN = "2.0"; ROSE_SKY = "6.0" } @("-nullrhi")
Log "JPT01 lighting"

RunUE "$TOOLS\ue5_import_npcs.py" "$LOGDIR\npcs.log" @{ ROSE_ZONE = "JPT01" } @()
Log "JPT01 NPCs placed"

# ══ GLOBAL FIXES (all maps, incl. fresh JPT01) ═══════════════════════════════
RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$LOGDIR\metal_switch.log" `
    @{ ROSE_ROOT = "/Game/Maps" } @("-nullrhi")
Log "metallic switch off (global)"

RunUE "$TOOLS\ue5_remove_metallic_factor.py" "$LOGDIR\metal_factor.log" `
    @{ ROSE_ROOT = "/Game" } @("-nullrhi")
Log "MetallicFactor override removed (global)"

RunUE "$TOOLS\ue5_foliage_master.py" "$LOGDIR\folmat.log" `
    @{ ROSE_ALL = "1"; ROSE_ALLMASKED = "1" } @("-nullrhi")
Log "masked M_GLTF instances reparented to M_RoseFoliage (global)"

RunUE "$TOOLS\ue5_foliage_nocollision.py" "$LOGDIR\folcol.log" @{ ROSE_ZONE = "ALL" } @()
Log "foliage collision removed (global)"

Log "=== JPT01 REBUILD + GLOBAL FIX COMPLETE ==="
