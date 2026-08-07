# Chunked, resumable NPC GLB import.
#
# Interchange bulk imports trip the GPU watchdog (TDR) somewhere above ~75 assets
# in one editor launch, and a TDR kills the whole run.  50 per launch is the
# documented-safe size.  ROSE_SKIP_EXISTING makes each launch pick up where the
# last one stopped, so a crash costs one chunk, not the batch.
#
# Usage:  powershell -File run_npc_import_chunks.ps1 [-ChunkSize 50] [-MaxChunks 60]

param(
    [int]$ChunkSize = 50,
    [int]$MaxChunks = 60
)

$ErrorActionPreference = "Continue"
$editor  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$project = "C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
$script  = "C:\rose-next-classic\tools\ue5_import_monsters.py"
$logdir  = "C:\rose-next-classic\tools\npc_chunks"
New-Item -ItemType Directory -Path $logdir -Force | Out-Null

$env:ROSE_SKIP_EXISTING = "1"
$env:ROSE_LIMIT = "$ChunkSize"

$total = 0
for ($i = 1; $i -le $MaxChunks; $i++) {
    $log = Join-Path $logdir ("chunk_{0:D3}.log" -f $i)

    # The editor MUST be closed for each launch — project-lock contention with an
    # open editor shows up as a D3D12 device-removed crash, not a lock error.
    Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3

    & $editor $project -ExecutePythonScript="$script" `
        -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -abslog="$log" | Out-Null

    # "[mon] done: N imported, M skipped" is the script's own tally.
    $done = Select-String -Path $log -Pattern "\[mon\] done: (\d+) imported" |
            ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Select-Object -Last 1
    if ($null -eq $done) { $done = 0 }
    $total += $done
    "{0}  chunk {1,3}: {2,3} imported (running total {3})" -f (Get-Date -Format "HH:mm:ss"), $i, $done, $total

    # Zero imported means every GLB already has its asset — the batch is complete.
    if ($done -eq 0) {
        "chunk {0} imported nothing - batch complete" -f $i
        break
    }
}
"=== NPC import finished: {0} imported ===" -f $total
