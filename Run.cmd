@echo off
pwsh -NoLogo -NoProfile -File "%~dp0Run.ps1" %*
if errorlevel 1 pause
