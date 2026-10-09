@echo off
pwsh -NoLogo -NoProfile -File "%~dp0RunLatestBuild.ps1" %*
set "VEEHIICUUL3_RUN_EXIT=%errorlevel%"
if not "%VEEHIICUUL3_RUN_EXIT%"=="0" pause
exit /b %VEEHIICUUL3_RUN_EXIT%
