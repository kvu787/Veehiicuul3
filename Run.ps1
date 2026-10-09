param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$Monitor='',
    [ValidateSet('None','Hidden','Smoke')][string]$TestMode='None',[switch]$Software,[string]$LogDirectory='')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exe = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'BuildInfo.json')) { Join-Path $PSScriptRoot 'Veehiicuul3.exe' } else { Join-Path $PSScriptRoot "BuildOutput\$Configuration\Veehiicuul3.exe" }
if (!(Test-Path -LiteralPath $exe)) { throw 'Build Veehiicuul3 first.' }
$runArguments = @()
if ($TestMode -ne 'None') {
    if ($Monitor -and $Monitor -ne 'NE18NZ2') { throw 'Assistant tests are restricted to NE18NZ2.' }
    $runArguments += @($(if ($TestMode -eq 'Hidden') { '--hidden-test' } else { '--smoke-test' }), '--test-monitor','NE18NZ2')
} elseif ($Monitor) { $runArguments += @('--monitor',('"' + $Monitor.Replace('"','') + '"')) }
if ($Software) { $runArguments += '--software' }
if ($LogDirectory) { $runArguments += @('--log-directory',('"' + $LogDirectory.Replace('"','') + '"')) }
$launch = @{FilePath=$exe; WorkingDirectory=$PSScriptRoot; PassThru=$true}
if ($runArguments.Count) { $launch.ArgumentList=$runArguments }
if ($TestMode -eq 'Hidden') { $launch.WindowStyle='Hidden' }
$process = Start-Process @launch
if ($TestMode -ne 'None') { $process.WaitForExit(); if ($process.ExitCode -ne 0) { throw "Test returned $($process.ExitCode)." } }
