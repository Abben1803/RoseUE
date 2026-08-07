# jpt01_rebuild_and_fix.ps1 — reimport JPT01 from the Arua client, then apply
# the global material/collision fixes across every map.
#
# JPT01: Arua export -> delete zone -> scene import -> mob spawners -> portals ->
#        ZSC material refit (Arua) -> classic lighting -> NPCs.
# Global (all maps, incl. fresh JPT01): metallic switch off -> MetallicFactor
#        override removed -> masked M_GLTF instances reparented to M_RoseFoliage
#        (fixes the universal black-in-cutout) -> foliage collision removed.
#
# Waits for the editor to close before EVERY step (safe to open/close mid-run).
# Progress: tools\_tmp\jpt01_rebuild.log
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$MAPFORGE = "C:\rose-next-classic\mapforge"
$ARUA = "C:\rose-next-classic\Arua_Extracted\3DDATA"
$CONTENT = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content"
$LOGDIR = "$TOOLS\_tmp\jpt01"
$PROGRESS = "$TOOLS\_tmp\jpt01_rebuild.log"
New-Item -ItemType Directory -Force $LOGDIR | Out-Null
$GLB = "$MAPFORGE\exports\JPT01.glb"

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

Log "=== JPT01 REBUILD + GLOBAL FIX START ==="

# ── 1. fresh Arua export (delete stale GLB first so a failed export can't be
#       mistaken for success) ────────────────────────────────────────────────
Remove-Item $GLB -ErrorAction SilentlyContinue
$env:MAPFORGE_ASSET_ROOT = $ARUA
Push-Location $MAPFORGE
py export_map.py JPT01 $GLB *> "$LOGDIR\export.txt"   # bare py = 3.14 (has numpy+PIL)
$exp = $LASTEXITCODE
Pop-Location
Remove-Item env:MAPFORGE_ASSET_ROOT -ErrorAction SilentlyContinue
if ($exp -ne 0 -or -not (Test-Path $GLB)) {
    Log "ABORT: Arua export of JPT01 failed (see $LOGDIR\export.txt) — JPT01 left untouched"
    exit 1
}
Log "JPT01 exported from Arua"

# ── 2. delete the old JPT01 zone (level + scene) ─────────────────────────────
RunUE "$TOOLS\ue5_delete_zone.py" "$LOGDIR\delete.log" @{ ROSE_ZONE = "JPT01" } @("-nullrhi")
Log "old JPT01 deleted"

# ── 3. scene import (needs RHI) ──────────────────────────────────────────────
RunUE "$TOOLS\ue5_import_map_scene.py" "$LOGDIR\scene.log" `
    @{ ROSE_MAP_GLB = $GLB; ROSE_MAP_ZONE = "JPT01" } @()
if (-not (Test-Path "$CONTENT\Maps\JPT01\L_JPT01.umap")) {
    Log "ABORT: JPT01 scene import failed (see $LOGDIR\scene.log)"
    exit 1
}
Log "JPT01 scene imported"

# ── 4. mob spawners ──────────────────────────────────────────────────────────
if (Test-Path "$TOOLS\_tmp\mob_spawns_JPT01.json") {
    RunUE "$TOOLS\ue5_import_mob_spawns.py" "$LOGDIR\mobs.log" @{ ROSE_ZONE = "JPT01" } @("-nullrhi")
    Log "JPT01 spawners placed"
}

# ── 5. warp portals ──────────────────────────────────────────────────────────
RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals.log" @{ ROSE_ZONE = "JPT01" } @("-nullrhi")
Log "JPT01 portals placed"

# ── 6. ZSC-faithful materials from the Arua packs ────────────────────────────
RunUE "$TOOLS\ue5_refit_map_mats.py" "$LOGDIR\refit.log" `
    @{ ROSE_ZONE = "JPT01"; ROSE_FORCE_SCENE = "1"; ROSE_ASSET_ROOT = $ARUA } @("-nullrhi")
Log "JPT01 materials refit"

# ── 7. classic-look lighting ─────────────────────────────────────────────────
RunUE "$TOOLS\ue5_fix_lighting.py" "$LOGDIR\light.log" `
    @{ ROSE_ZONE = "JPT01"; ROSE_SUN = "2.0"; ROSE_SKY = "6.0" } @("-nullrhi")
Log "JPT01 lighting"

# ── 8. NPCs ──────────────────────────────────────────────────────────────────
RunUE "$TOOLS\ue5_import_npcs.py" "$LOGDIR\npcs.log" @{ ROSE_ZONE = "JPT01" } @()
Log "JPT01 NPCs placed"

# ══ GLOBAL FIXES (all maps, incl. fresh JPT01) ═══════════════════════════════
# 9. metallic-roughness switch OFF (Arua pipeline default)
RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$LOGDIR\metal_switch.log" `
    @{ ROSE_ROOT = "/Game/Maps" } @("-nullrhi")
Log "metallic switch off (global)"

# 10. MetallicFactor override removed (the checkbox the user untick'd)
RunUE "$TOOLS\ue5_remove_metallic_factor.py" "$LOGDIR\metal_factor.log" `
    @{ ROSE_ROOT = "/Game" } @("-nullrhi")
Log "MetallicFactor override removed (global)"

# 11. reparent EVERY masked M_GLTF instance -> M_RoseFoliage (fix black-in-cutout)
RunUE "$TOOLS\ue5_foliage_master.py" "$LOGDIR\folmat.log" `
    @{ ROSE_ALL = "1"; ROSE_ALLMASKED = "1" } @("-nullrhi")
Log "masked M_GLTF instances reparented to M_RoseFoliage (global)"

# 12. foliage collision removed (walk-through flowers/bushes/grass/trees)
RunUE "$TOOLS\ue5_foliage_nocollision.py" "$LOGDIR\folcol.log" @{ ROSE_ZONE = "ALL" } @()
Log "foliage collision removed (global)"

Log "=== JPT01 REBUILD + GLOBAL FIX COMPLETE ==="
