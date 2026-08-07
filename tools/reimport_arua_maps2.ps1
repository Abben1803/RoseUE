# reimport_arua_maps.ps1 — Re-import every existing UE zone from the ARUA
# (modern) client.  Per zone: mapforge re-export (MAPFORGE_ASSET_ROOT=Arua) →
# delete /Game/Maps/<Z> → Interchange scene import → mob spawners → portals →
# ZSC-faithful material refit (Arua packs) → lighting SUN=2:SKY=6.
# Afterwards one global bHasMetallicRoughness=off pass over /Game/Maps.
#
# Zones: only those already in the project.  EXCLUDED: JPT01 (already the Arua
# import, promoted), JG01 (not present in the Arua client — keeps classic).
#
# Pauses whenever the editor is open.  Progress: tools\_tmp\arua_progress.log
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$MAPFORGE = "C:\rose-next-classic\mapforge"
$CONTENT = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content"
$ARUA = "C:\rose-next-classic\Arua_Extracted\3DDATA"
$LOGDIR = "$TOOLS\_tmp\arua"
$PROGRESS = "$TOOLS\_tmp\arua_progress.log"
New-Item -ItemType Directory -Force $LOGDIR | Out-Null

$ZONES = @("JDT01")

function Log($msg) {
    Add-Content -Path $PROGRESS -Value ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $msg)
}

function WaitEditorClosed() {
    $warned = $false
    while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
        if (-not $warned) { Log "PAUSED: UnrealEditor open - close it to continue"; $warned = $true }
        Start-Sleep -Seconds 30
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

Log "=== ARUA REIMPORT START ($($ZONES.Count) zones) ==="

foreach ($z in $ZONES) {
    Log "ZONE $z ..."
    $glb = "$MAPFORGE\exports\$z.glb"
    $level = "$CONTENT\Maps\$z\L_$z.umap"

    # 1 — fresh export from Arua (always: the on-disk GLB is the classic one)
    $env:MAPFORGE_ASSET_ROOT = $ARUA
    Push-Location $MAPFORGE
    py export_map.py $z $glb *> "$LOGDIR\export_$z.txt"
    Pop-Location
    Remove-Item env:MAPFORGE_ASSET_ROOT -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $glb)) {
        Log "ZONE $z FAIL: arua export (see export_$z.txt) - zone kept as-is"
        continue
    }
    Log "ZONE $z exported from Arua"

    # 2 — delete the old zone folder (level + scene)
    RunUE "$TOOLS\ue5_delete_zone.py" "$LOGDIR\del_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")

    # 3 — scene import (needs RHI; one zone per process like run_all_maps)
    RunUE "$TOOLS\ue5_import_map_scene.py" "$LOGDIR\scene_$z.log" `
        @{ ROSE_MAP_GLB = $glb; ROSE_MAP_ZONE = $z } @()
    if (-not (Test-Path $level)) { Log "ZONE $z FAIL: scene import (see scene_$z.log)"; continue }
    Log "ZONE $z level imported"

    # 4 — mob spawners (when the zone has spawn points)
    $spawnJson = "$TOOLS\_tmp\mob_spawns_$z.json"
    if (Test-Path $spawnJson) {
        $pts = (Get-Content $spawnJson -Raw | ConvertFrom-Json).points.Count
        if ($pts -gt 0) {
            RunUE "$TOOLS\ue5_import_mob_spawns.py" "$LOGDIR\mobs_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")
            Log "ZONE $z spawners: $pts points"
        }
    }

    # 5 — warp portals
    RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")

    # 6 — ZSC-faithful scene materials from the Arua packs
    RunUE "$TOOLS\ue5_refit_map_mats.py" "$LOGDIR\refit_$z.log" `
        @{ ROSE_ZONE = $z; ROSE_FORCE_SCENE = "1"; ROSE_ASSET_ROOT = $ARUA } @("-nullrhi")

    Log "ZONE $z done"

}

# global: glTF metallic-roughness OFF on every imported scene material
RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$LOGDIR\metal_all.log" `
    @{ ROSE_ROOT = "/Game/Maps" } @("-nullrhi")
Log "metallic switch pass done"

Log "=== ARUA REIMPORT COMPLETE ==="
