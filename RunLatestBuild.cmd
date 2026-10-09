@echo off
pwsh -NoLogo -NoProfile -File "%~dp0RunLatestBuild.ps1" %*
set "VEEHIICUUL3_RUN_EXIT=%errorlevel%"
set "VEEHIICUUL3_AUTOMATED="
for %%A in (%*) do if /I "%%~A"=="-TestMode" set "VEEHIICUUL3_AUTOMATED=1"
if not "%VEEHIICUUL3_RUN_EXIT%"=="0" if not defined VEEHIICUUL3_AUTOMATED pause
exit /b %VEEHIICUUL3_RUN_EXIT%
