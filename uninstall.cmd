@echo off
setlocal
set "scriptPath=%~dp0uninstall.ps1"
if not exist "%scriptPath%" set "scriptPath=%~dp0scripts\uninstall.ps1"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%scriptPath%" -Pause %*
exit /b %errorlevel%
