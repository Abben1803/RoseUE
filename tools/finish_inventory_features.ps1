# finish_inventory_features.ps1 — the editor-bound tail of the three inventory
# features (ghosts / tooltips / consumables).  Everything offline is already done;
# these four steps need exclusive use of the editor DLL, so run this when no
# other headless UE job is running.
#
#   powershell -File tools\finish_inventory_features.ps1
#
# It waits for any UnrealEditor-Cmd to exit before each step, so it is safe to
# start while another pipeline is finishing.

$ErrorActionPreference = "Stop"
$UE      = "C:\Program Files\Epic Games\UE_5.8"
$PROJECT = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$CMD     = "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

function Wait-ForIdleEditor {
    while (Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
        Write-Host "[finish] another headless UE job is running - waiting..."
        Start-Sleep -Seconds 15
    }
}

function Invoke-UEPython([string]$Script) {
    Wait-ForIdleEditor
    Write-Host "[finish] === $Script ==="
    # -abslog is not reliably written under this project path; read stdout.
    & $CMD $PROJECT "-ExecutePythonScript=$Script" -unattended -nopause -nosplash `
        -stdout -FullStdOutLogOutput 2>&1 |
        Select-String -Pattern '\[uispr\]|\[dt\]|\[probe\]|Error:' |
        ForEach-Object { $_.Line }
}

# 1. Link the editor DLL.  All objects already compile clean; only the link
#    stage was blocked, so this is quick.
Wait-ForIdleEditor
Write-Host "[finish] === compiling RoseUEEditor ==="
$out = & "$UE\Engine\Build\BatchFiles\Build.bat" RoseUEEditor Win64 Development `
         -project="$PROJECT" -waitmutex 2>&1
$out | Select-Object -Last 8
if ($out -notmatch "Result: Succeeded") {
    Write-Host "[finish] BUILD FAILED - stopping (nothing below will pick up the new struct)"
    exit 1
}

# 2. Import the 14 slot-ghost PNGs as UI textures (/Game/UI/SlotGhosts).
$env:ROSE_UI_ONLY = "SlotGhosts"
Invoke-UEPython "C:\rose-next-classic\tools\ue5_import_ui_sprites.py"
Remove-Item Env:\ROSE_UI_ONLY

# 3. Reimport the DataTables.  REQUIRED: FRoseSimpleItemRow grew the LIST_USEITEM
#    effect block, and every gen_simple table shares that struct.
Invoke-UEPython "C:\rose-next-classic\tools\ue5_import_datatables.py"

# 4. Verify all three features through the exact runtime lookup paths.
Invoke-UEPython "C:\rose-next-classic\tools\ue5_probe_inventory.py"
