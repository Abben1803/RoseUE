$ErrorActionPreference = 'SilentlyContinue'
$flt = '*-File*post_charselect.ps1*'

# kill any existing -File runners (this script is launched with -Command so it
# won't self-match the -File filter)
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
    Where-Object { $_.CommandLine -like $flt } |
    ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
Start-Sleep 4
$n = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like $flt }).Count
if ($n -gt 0) { Write-Output "ABORT: $n still alive"; return }

Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'C:\rose-next-classic\tools\post_charselect.ps1' -WindowStyle Hidden
Start-Sleep 6
$n2 = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like $flt }).Count
Write-Output "post_charselect runners now: $n2"
