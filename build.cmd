@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Pause %*
exit /b %errorlevel%
