@echo off
REM ============================================================================
REM  import_headless.bat  —  Run the ROSE character import with NO rendering.
REM
REM  The interactive editor crashes the GPU during bulk skeletal-mesh import
REM  because it renders the viewport (virtual textures, post-processing, GPU
REM  skin cache) at the same time.  This runs the import as a commandlet with
REM  -nullrhi: no viewport, no skin cache, no GPU work at all.  Pure CPU import.
REM
REM  Output is logged to tools\import_headless.log for review.
REM ============================================================================

set "ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "PROJECT=C:\rose-next-classic\unreal-engine rose\RoseUE\RoseUE.uproject"
set "SCRIPT=C:\rose-next-classic\tools\ue5_import_characters.py"
set "LOG=C:\rose-next-classic\tools\import_headless.log"

echo Running headless import...
echo   Engine : %ENGINE%
echo   Project: %PROJECT%
echo   Script : %SCRIPT%
echo   Log    : %LOG%
echo.

"%ENGINE%" "%PROJECT%" ^
    -ExecutePythonScript="%SCRIPT%" ^
    -unattended ^
    -nopause ^
    -nosplash ^
    -nullrhi ^
    -stdout ^
    -FullStdOutLogOutput ^
    -abslog="%LOG%"

echo.
echo ============================================================================
echo  Import finished. Exit code: %ERRORLEVEL%
echo  Full log: %LOG%
echo ============================================================================
pause
