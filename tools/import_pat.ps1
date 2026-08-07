# import_pat.ps1 — chunked PAT part import driver.
# One UE process per (pet family, chunk) — dodges the Interchange skeleton
# state-contamination assertion and keeps GPU sessions short (TDR).
$ErrorActionPreference = "Continue"
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$PROJ = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$TOOLS = "C:\rose-next-classic\tools"
$LOGDIR = "$TOOLS\_tmp\pat_import"
New-Item -ItemType Directory -Force $LOGDIR | Out-Null
$CHUNK = 60

foreach ($pet in 21, 31) {
    # pet 21 now carries the mounts too (246 cart + 157 mount), so the window
    # has to reach past the old 500-part ceiling.
    for ($start = 0; $start -lt 700; $start += $CHUNK) {
        while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 30 }
        $env:ROSE_PAT_PET = "$pet"
        $env:ROSE_PAT_START = "$start"
        $env:ROSE_PAT_COUNT = "$CHUNK"
        $log = "$LOGDIR\pat_${pet}_$start.log"
        # Capture stdout to the log file directly. Do NOT combine -stdout with
        # -abslog and then `*> $null`: -stdout routes the log to stdout, -abslog
        # is overridden by it, and the redirect discards the lot — which is how
        # this silently produced empty log dirs.
        & $UE $PROJ -ExecutePythonScript="$TOOLS\ue5_import_pat.py" -unattended -nopause `
            -nosplash -stdout -FullStdOutLogOutput 2>&1 | Out-File -Encoding utf8 $log
        $done = Select-String -Path $log -Pattern "patimport\] DONE" -SimpleMatch -Quiet
        $line = (Select-String -Path $log -Pattern "patimport\] (DONE|importing)" | Select-Object -Last 1).Line
        Write-Output "pet $pet chunk $start : exit=$LASTEXITCODE $line"
        # empty chunk window -> next family
        if ($line -match "importing 0 parts") { break }
    }
}
Remove-Item env:ROSE_PAT_PET, env:ROSE_PAT_START, env:ROSE_PAT_COUNT -ErrorAction SilentlyContinue
Write-Output "PAT IMPORT COMPLETE"
