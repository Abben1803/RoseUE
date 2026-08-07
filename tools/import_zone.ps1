# import_zone.ps1 — ONE-COMMAND zone import: everything lands "from the start",
# no manual post-passes. Per zone:
#   1. mapforge export (MAPFORGE_ASSET_ROOT, default Arua)
#   2. Interchange scene import (nanite OFF + complex-as-simple built in)
#   3. ZSC part manifest (validated node->part mapping)
#   4. ZSC part flags: per-part collision + anim tags + RoseMapAnimManager
#   5. mob spawners + warp portals (when data exists)
#   6. ZSC-faithful scene materials (refit, 1:1 zz_material semantics)
#   7. classic-look lighting (SUN=2 : SKY=6) + glTF metallic switch off
#
# Usage:  powershell -File tools\import_zone.ps1 -Zones JPT01,JD01 [-Classic]
param(
    [Parameter(Mandatory = $true)][string[]]$Zones,
    [switch]$Classic
)
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$MAPFORGE = "C:\rose-next-classic\mapforge"
$CONTENT = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content"
$ROOT = if ($Classic) { "C:\rose-next-classic\client\out\3DDATA" } else { "C:\rose-next-classic\Arua_Extracted\3DDATA" }
$LOGDIR = "$TOOLS\_tmp\import_zone"
New-Item -ItemType Directory -Force $LOGDIR | Out-Null

function RunUE($script, $log, $envs, $extra) {
    while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
        Write-Host "PAUSED: close the Unreal editor to continue"; Start-Sleep -Seconds 30
    }
    foreach ($k in $envs.Keys) { Set-Item -Path "env:$k" -Value $envs[$k] }
    $a = @($PROJ) + $extra + @("-ExecutePythonScript=$script", "-unattended", "-nopause",
                               "-nosplash", "-stdout", "-FullStdOutLogOutput", "-abslog=$log")
    & $UE @a *> $null
    foreach ($k in $envs.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}

foreach ($z in $Zones) {
    Write-Host "=== ZONE $z (root: $ROOT) ==="
    $glb = "$MAPFORGE\exports\$z.glb"

    # 1 — export
    $env:MAPFORGE_ASSET_ROOT = $ROOT
    Push-Location $MAPFORGE
    py -3.14 export_map.py $z $glb *> "$LOGDIR\export_$z.txt"
    Pop-Location
    Remove-Item env:MAPFORGE_ASSET_ROOT -ErrorAction SilentlyContinue
    if (-not (Test-Path $glb)) { Write-Host "FAIL export (see export_$z.txt)"; continue }

    # 2 — scene import (RHI needed)
    RunUE "$TOOLS\ue5_import_map_scene.py" "$LOGDIR\scene_$z.log" `
        @{ ROSE_MAP_GLB = $glb; ROSE_MAP_ZONE = $z } @()
    if (-not (Test-Path "$CONTENT\Maps\$z\L_$z.umap")) { Write-Host "FAIL scene import"; continue }

    # 3 — ZSC part manifest (validated against the GLB just imported)
    $env:MAPFORGE_ASSET_ROOT = $ROOT
    $env:ROSE_ZONES = $z
    py -3.14 "$TOOLS\gen_zsc_part_manifest.py" *> "$LOGDIR\manifest_$z.txt"
    Remove-Item env:MAPFORGE_ASSET_ROOT, env:ROSE_ZONES -ErrorAction SilentlyContinue

    # 4 — per-part collision + anim tags + anim manager
    RunUE "$TOOLS\ue5_apply_zsc_part_flags.py" "$LOGDIR\flags_$z.log" `
        @{ ROSE_ZONES = $z } @("-nullrhi")

    # 5 — mob spawners + portals
    if (Test-Path "$TOOLS\_tmp\mob_spawns_$z.json") {
        RunUE "$TOOLS\ue5_import_mob_spawns.py" "$LOGDIR\mobs_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")
    }
    RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")

    # 6 — ZSC-faithful scene materials
    RunUE "$TOOLS\ue5_refit_map_mats.py" "$LOGDIR\refit_$z.log" `
        @{ ROSE_ZONE = $z; ROSE_FORCE_SCENE = "1"; ROSE_ASSET_ROOT = $ROOT } @("-nullrhi")

    # 7 — lighting + metallic
    RunUE "$TOOLS\ue5_fix_lighting.py" "$LOGDIR\light_$z.log" `
        @{ ROSE_ZONE = $z; ROSE_SUN = "2.0"; ROSE_SKY = "6.0" } @("-nullrhi")
    RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$LOGDIR\metal_$z.log" `
        @{ ROSE_ROOT = "/Game/Maps/$z" } @("-nullrhi")

    Write-Host "=== ZONE $z DONE ==="
}
