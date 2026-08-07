# run_all_maps.ps1 — Batch-build EVERY zone: mapforge GLB export, UE scene
# import, mob spawners, texture atlas + master materials, warp portals.
# Fully resumable (each step skips work that already exists).  Pauses whenever
# the editor is open.  Progress: tools\_tmp\allmaps_progress.log
#
# Run detached:  Start-Process powershell -ArgumentList '-ExecutionPolicy','Bypass','-File','tools\run_all_maps.ps1'

$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$MAPFORGE = "C:\rose-next-classic\mapforge"
$CONTENT = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content"
$LOGDIR = "$TOOLS\_tmp\allmaps"
$PROGRESS = "$TOOLS\_tmp\allmaps_progress.log"
New-Item -ItemType Directory -Force $LOGDIR | Out-Null

# JPT01 first (fully done already = cheap driver smoke test), then the rest.
$ZONES = @("JPT01","JDT01","JG01","JG02","JG03","JG04","JG05","JG06","JG07","JG08",
           "JD01","JD02","JD03","JD04","JZ01_1","JZ01_2","JZ01_3","JMOV01",
           "AGIT01","JPVP01","JPVP02","JPVP04","JPVP05","TITLE_JPT","SUM_EVENT",
           "LMT01","LP01","LP02","LP03","LP04","LP05","LZ01","LZ02","LPVP01",
           "EJT01","EJ01","EJ02","EJ03","EZ01")

function Log($msg) {
    $line = "{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $msg
    Add-Content -Path $PROGRESS -Value $line
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

Log "=== ALL-MAPS BATCH START ($($ZONES.Count) zones) ==="

# ── Phase 0: mob spawn exports + every referenced monster ────────────────────
Log "PHASE 0: mob spawn data + monsters"
foreach ($z in $ZONES) {
    py -3.9 "$TOOLS\export_mob_spawns.py" $z *> "$LOGDIR\spawns_$z.txt"
}
$ids = py -3.9 -c "import json,glob; ids=set()
for f in glob.glob(r'$TOOLS\_tmp\mob_spawns_*.json'):
    d=json.load(open(f))
    for p in d['points']:
        for e in p['basic']+p['tactic']: ids.add(e['npc_id'])
print(','.join(map(str,sorted(i for i in ids if i>0))))"
Log "PHASE 0: $((($ids -split ',') | Measure-Object).Count) unique mob ids referenced"

$env:ROSE_ONLY = $ids
py -3.9 "$TOOLS\build_monsters.py" *> "$LOGDIR\build_monsters.txt"
Remove-Item env:ROSE_ONLY -ErrorAction SilentlyContinue
Log "PHASE 0: monster GLBs built (see build_monsters.txt)"

# Import in chunks of 40 per process (RHI; bulk single-process import TDRs).
for ($i = 0; $i -lt 40; $i++) {
    RunUE "$TOOLS\ue5_import_monsters.py" "$LOGDIR\mon_import_$i.log" `
        @{ ROSE_SKIP_EXISTING = "1"; ROSE_LIMIT = "40" } @()
    $done = (Select-String "$LOGDIR\mon_import_$i.log" -Pattern "\[mon\] done: (\d+) imported").Matches
    $n = if ($done.Count -gt 0) { [int]$done[0].Groups[1].Value } else { -1 }
    Log "PHASE 0: monster import chunk $i -> $n imported"
    if ($n -eq 0) { break }
}
# Master-material MIs for the new monsters (atlas pages already cover all mobs).
RunUE "$TOOLS\ue5_apply_master_materials.py" "$LOGDIR\mon_apply.log" `
    @{ ROSE_CATS = "monsters" } @("-nullrhi")
Log "PHASE 0 done"

# ── Phase 1: per-zone scene/spawns/atlas/portals ─────────────────────────────
foreach ($z in $ZONES) {
    Log "ZONE $z ..."
    $glb = "$MAPFORGE\exports\$z.glb"
    $level = "$CONTENT\Maps\$z\L_$z.umap"

    if (-not (Test-Path $glb)) {
        Push-Location $MAPFORGE
        py export_map.py $z $glb *> "$LOGDIR\export_$z.txt"
        Pop-Location
        if (-not (Test-Path $glb)) { Log "ZONE $z FAIL: mapforge export (see export_$z.txt)"; continue }
        Log "ZONE $z glb exported"
    }

    if (-not (Test-Path $level)) {
        RunUE "$TOOLS\ue5_import_map_scene.py" "$LOGDIR\scene_$z.log" `
            @{ ROSE_MAP_GLB = $glb; ROSE_MAP_ZONE = $z } @()
        if (-not (Test-Path $level)) { Log "ZONE $z FAIL: scene import (see scene_$z.log)"; continue }
        Log "ZONE $z level imported"
    }

    # mob spawners (only when the zone has spawn points)
    $spawnJson = "$TOOLS\_tmp\mob_spawns_$z.json"
    if (Test-Path $spawnJson) {
        $pts = (Get-Content $spawnJson -Raw | ConvertFrom-Json).points.Count
        if ($pts -gt 0) {
            RunUE "$TOOLS\ue5_import_mob_spawns.py" "$LOGDIR\mobs_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")
            Log "ZONE $z spawners: $pts points"
        }
    }

    # atlas + master materials for the scene
    py -3.9 "$TOOLS\gen_map_texture_manifest.py" $z *> "$LOGDIR\mapman_$z.txt"
    py -3.9 "$TOOLS\build_texture_atlases.py" "_tmp\map_texture_manifest_$z.json" "_tmp\map_atlas_manifest_$z.json" *> "$LOGDIR\pack_$z.txt"
    RunUE "$TOOLS\ue5_import_atlases.py" "$LOGDIR\pages_$z.log" `
        @{ ROSE_ATLAS_MANIFEST = "_tmp\map_atlas_manifest_$z.json" } @()
    RunUE "$TOOLS\ue5_apply_map_materials.py" "$LOGDIR\apply_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")
    Log "ZONE $z atlas applied"

    # warp portals
    RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi")
    Log "ZONE $z done"
}

Log "=== ALL-MAPS BATCH COMPLETE ==="
