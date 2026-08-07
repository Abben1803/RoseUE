# post_charselect.ps1 — after the post-batch material passes + DLL compile
# finish, build the Character Select level and boot the game into it.
# Gated on a SUCCESSFUL compile (the char-select classes must exist in the DLL).
# Progress: tools\_tmp\charselect.log
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$INI = "C:\rose-next-classic\unreal-engine rose\RoseUE\Config\DefaultEngine.ini"
$OUT = "$TOOLS\_tmp\charselect.log"

function Log($m) { Add-Content -Path $OUT -Value ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }

Log "waiting for post-batch material passes + compile to finish..."
while (-not (Select-String -Path "$TOOLS\_tmp\post_arua.log" -Pattern "POST-BATCH COMPLETE" -Quiet -ErrorAction SilentlyContinue)) {
    Start-Sleep -Seconds 60
}
# require the DLL compile to have SUCCEEDED (else the char-select classes aren't in the DLL)
if (-not (Select-String -Path "$TOOLS\_tmp\post_arua.log" -Pattern "DLL compile: SUCCEEDED" -Quiet)) {
    Log "ABORT: post-batch DLL compile did not succeed - not building char select (game still boots to JPT01)"
    exit 1
}
# make sure every UE process is gone before we open the editor
while ((Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue) -or (Get-Process UnrealEditor -ErrorAction SilentlyContinue)) {
    Start-Sleep -Seconds 20
}
Log "compile OK - reimporting item DataTables (weight column)"
& $UE $PROJ -nullrhi "-ExecutePythonScript=$TOOLS\ue5_import_datatables.py" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput "-abslog=$TOOLS\_tmp\dt_reimport_weight.log" *> $null
Log "DataTables reimported (see _tmp\dt_reimport_weight.log)"

# ── loading-screen textures (ELDEON/JUNON/LUNAR) — needs RHI ──
& $UE $PROJ "-ExecutePythonScript=$TOOLS\ue5_import_loading.py" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput "-abslog=$TOOLS\_tmp\loading_import.log" *> $null
Log "loading-screen textures imported (see _tmp\loading_import.log)"

# ── JPVP05 from the CLASSIC client (Arua was missing terrain tile 35_30) ──
$CLASSIC = "C:\rose-next-classic\client\out\3DDATA"
$glb5 = "C:\rose-next-classic\mapforge\exports\JPVP05.glb"
function RunUE5($script, $log, $envs, $rhi) {
    foreach ($k in $envs.Keys) { Set-Item -Path "env:$k" -Value $envs[$k] }
    $extra = if ($rhi) { @() } else { @("-nullrhi") }
    $a = @($PROJ) + $extra + @("-ExecutePythonScript=$script","-unattended","-nopause","-nosplash","-stdout","-FullStdOutLogOutput","-abslog=$log")
    & $UE @a *> $null
    foreach ($k in $envs.Keys) { Remove-Item "env:$k" -EA SilentlyContinue }
}
if (Test-Path $glb5) {
    Log "importing JPVP05 from classic client"
    RunUE5 "$TOOLS\ue5_delete_zone.py"       "$TOOLS\_tmp\jpvp05_del.log"    @{ ROSE_ZONE="JPVP05" } $false
    RunUE5 "$TOOLS\ue5_import_map_scene.py"   "$TOOLS\_tmp\jpvp05_scene.log"  @{ ROSE_MAP_GLB=$glb5; ROSE_MAP_ZONE="JPVP05" } $true
    RunUE5 "$TOOLS\ue5_import_portals.py"     "$TOOLS\_tmp\jpvp05_portals.log" @{ ROSE_ZONE="JPVP05" } $false
    RunUE5 "$TOOLS\ue5_refit_map_mats.py"     "$TOOLS\_tmp\jpvp05_refit.log"  @{ ROSE_ZONE="JPVP05"; ROSE_FORCE_SCENE="1"; ROSE_ASSET_ROOT=$CLASSIC } $false
    RunUE5 "$TOOLS\ue5_fix_metallic_switch.py" "$TOOLS\_tmp\jpvp05_metal.log" @{ ROSE_ROOT="/Game/Maps/JPVP05" } $false
    RunUE5 "$TOOLS\ue5_fix_lighting.py"       "$TOOLS\_tmp\jpvp05_light.log"  @{ ROSE_ZONE="JPVP05"; ROSE_SUN="2.0"; ROSE_SKY="6.0" } $false
    $ok5 = Test-Path "C:\rose-next-classic\unreal-engine rose\RoseUE\Content\Maps\JPVP05\L_JPVP05.umap"
    Log "JPVP05 import: $(if ($ok5) { 'OK' } else { 'FAILED (see _tmp\jpvp05_*.log)' })"
} else {
    Log "JPVP05 GLB missing - skipped"
}

Log "building Character Select level"
& $UE $PROJ -nullrhi "-ExecutePythonScript=$TOOLS\ue5_build_charselect.py" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput "-abslog=$TOOLS\_tmp\charselect_build.log" *> $null

$built = Test-Path "C:\rose-next-classic\unreal-engine rose\RoseUE\Content\Maps\CharacterSelect\L_CharacterSelect.umap"
if (-not $built) {
    Log "FAILED: L_CharacterSelect.umap not produced (see _tmp\charselect_build.log) - GameDefaultMap left at JPT01"
    exit 1
}
Log "level built"

# boot the game into char select
$ini = Get-Content $INI -Raw
$ini = $ini -replace "GameDefaultMap=/Game/Maps/JPT01/L_JPT01", "GameDefaultMap=/Game/Maps/CharacterSelect/L_CharacterSelect"
Set-Content -Path $INI -Value $ini -Encoding UTF8
Log "GameDefaultMap -> CharacterSelect"
Log "=== CHAR SELECT COMPLETE ==="
