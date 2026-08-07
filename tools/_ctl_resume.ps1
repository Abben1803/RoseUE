$ErrorActionPreference = 'SilentlyContinue'
$flt = '*-File*resume_arua_maps.ps1*'

function KillAll {
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
        Where-Object { $_.CommandLine -like $flt } |
        ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
    Get-Process UnrealEditor-Cmd, UnrealEditor | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
}

KillAll; Start-Sleep 4
KillAll; Start-Sleep 4

$n = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like $flt }).Count
$u = @(Get-Process UnrealEditor-Cmd, UnrealEditor).Count
if ($n -gt 0 -or $u -gt 0) { Write-Output "ABORT: still alive resume=$n UE=$u"; return }

Remove-Item "C:\rose-next-classic\tools\_tmp\arua\del_LP03.log" -Force
Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'C:\rose-next-classic\tools\resume_arua_maps.ps1' -WindowStyle Hidden
Start-Sleep 8
$n2 = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like $flt }).Count
$u2 = @(Get-Process UnrealEditor-Cmd, UnrealEditor).Count
Write-Output "LAUNCHED: resume=$n2 UE=$u2"
