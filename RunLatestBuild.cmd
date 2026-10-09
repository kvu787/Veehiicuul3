@echo off
pwsh -NoLogo -NoProfile -File "%~dp0RunLatestBuild.ps1" %*
if errorlevel 1 pause
