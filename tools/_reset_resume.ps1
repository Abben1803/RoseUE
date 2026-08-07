$ErrorActionPreference = 'SilentlyContinue'
# Kill every resume runner and any Unreal process, then report a clean count.
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
  Where-Object { $_.CommandLine -like '*resume_arua*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Get-Process UnrealEditor-Cmd, UnrealEditor | Stop-Process -Force
Start-Sleep -Seconds 5
$rc = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like '*resume_arua*' }).Count
$uc = @(Get-Process UnrealEditor-Cmd, UnrealEditor).Count
Write-Output "after-reset: resume=$rc UE=$uc"
