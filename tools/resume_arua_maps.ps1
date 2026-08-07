# resume_arua_maps.ps1 — resume the Arua reimport from LP03 (the run stalled
# when the machine slept mid-delete).  Processes only the remaining zones, then
# the final project-wide metallic pass (writes metal_all.log, which the waiting
# post_arua_batch.ps1 keys off).
#
# Hardening vs the original: (1) keeps the system awake for the whole run;
# (2) each editor step has a timeout — a hung step is killed + skipped instead
# of blocking the batch forever.
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$MAPFORGE = "C:\rose-next-classic\mapforge"
$CONTENT = "C:\rose-next-classic\unreal-engine rose\RoseUE\Content"
$ARUA = "C:\rose-next-classic\Arua_Extracted\3DDATA"
$LOGDIR = "$TOOLS\_tmp\arua"
$PROGRESS = "$TOOLS\_tmp\arua_progress.log"

$ZONES = @("LP03","LP04","LP05","LPVP01","LZ01","LZ02","SUM_EVENT","TITLE_JPT")

# keep the system awake (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) for this process's life
Add-Type -Name Power -Namespace Win32 -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);'
[void][Win32.Power]::SetThreadExecutionState(0x80000001)

function Log($msg) { Add-Content -Path $PROGRESS -Value ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $msg) }

function WaitEditorClosed() {
    while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 30 }
}

# Run an editor step with a timeout (minutes). Kills + returns on overrun.
function RunUE($script, $log, $envs, $extra, $timeoutMin) {
    WaitEditorClosed
    foreach ($k in $envs.Keys) { Set-Item -Path "env:$k" -Value $envs[$k] }
    $argStr = ('"{0}"' -f $PROJ)
    foreach ($e in $extra) { $argStr += " $e" }
    $argStr += (' -ExecutePythonScript="{0}" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -abslog="{1}"' -f $script, $log)
    $p = Start-Process -FilePath $UE -ArgumentList $argStr -PassThru -WindowStyle Hidden
    if (-not $p.WaitForExit($timeoutMin * 60000)) {
        Log "  TIMEOUT ${timeoutMin}m - killing $([IO.Path]::GetFileNameWithoutExtension($script))"
        try { $p.Kill($true) } catch {}
        Start-Sleep -Seconds 5
    }
    foreach ($k in $envs.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}

Log "=== ARUA RESUME START ($($ZONES.Count) zones: $($ZONES -join ',')) ==="

foreach ($z in $ZONES) {
    Log "ZONE $z ..."
    $glb = "$MAPFORGE\exports\$z.glb"
    $level = "$CONTENT\Maps\$z\L_$z.umap"

    # 1 — fresh export from Arua
    $env:MAPFORGE_ASSET_ROOT = $ARUA
    Push-Location $MAPFORGE
    py export_map.py $z $glb *> "$LOGDIR\export_$z.txt"
    Pop-Location
    Remove-Item env:MAPFORGE_ASSET_ROOT -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $glb)) {
        Log "ZONE $z FAIL: arua export (see export_$z.txt) - kept as-is"; continue
    }
    Log "ZONE $z exported"

    RunUE "$TOOLS\ue5_delete_zone.py" "$LOGDIR\del_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi") 15
    RunUE "$TOOLS\ue5_import_map_scene.py" "$LOGDIR\scene_$z.log" @{ ROSE_MAP_GLB = $glb; ROSE_MAP_ZONE = $z } @() 40
    if (-not (Test-Path $level)) { Log "ZONE $z FAIL: scene import (see scene_$z.log)"; continue }
    Log "ZONE $z level imported"

    $spawnJson = "$TOOLS\_tmp\mob_spawns_$z.json"
    if (Test-Path $spawnJson) {
        $pts = (Get-Content $spawnJson -Raw | ConvertFrom-Json).points.Count
        if ($pts -gt 0) { RunUE "$TOOLS\ue5_import_mob_spawns.py" "$LOGDIR\mobs_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi") 15 }
    }
    RunUE "$TOOLS\ue5_import_portals.py" "$LOGDIR\portals_$z.log" @{ ROSE_ZONE = $z } @("-nullrhi") 15
    RunUE "$TOOLS\ue5_refit_map_mats.py" "$LOGDIR\refit_$z.log" `
        @{ ROSE_ZONE = $z; ROSE_FORCE_SCENE = "1"; ROSE_ASSET_ROOT = $ARUA } @("-nullrhi") 20
    RunUE "$TOOLS\ue5_fix_lighting.py" "$LOGDIR\light_$z.log" `
        @{ ROSE_ZONE = $z; ROSE_SUN = "2.0"; ROSE_SKY = "6.0" } @("-nullrhi") 15
    Log "ZONE $z done"
}

# final project-wide metallic OFF (writes metal_all.log -> post_arua_batch proceeds)
RunUE "$TOOLS\ue5_fix_metallic_switch.py" "$LOGDIR\metal_all.log" @{ ROSE_ROOT = "/Game/Maps" } @("-nullrhi") 40
Log "metallic switch pass done"
Log "=== ARUA REIMPORT COMPLETE ==="

[void][Win32.Power]::SetThreadExecutionState(0x80000000)  # release keep-awake
