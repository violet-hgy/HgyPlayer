@echo off
setlocal
cd /d "%~dp0"

REM One-click package entry -> build.ps1
REM Usage:
REM   build.bat
REM   build.bat -Clean
REM   build.bat -QtVersion 6.10.2 -NoZip

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set ERR=%ERRORLEVEL%
if %ERR% neq 0 (
  echo.
  echo Build failed with exit code %ERR%.
  pause
  exit /b %ERR%
)

echo.
pause
endlocal
