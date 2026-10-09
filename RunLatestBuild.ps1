param(
    [string]$Candidate = '',
    [string]$Monitor = '',
    [ValidateSet('None','Hidden','Smoke')][string]$TestMode = 'None',
    [string]$LogDirectory = '',
    [switch]$Wait
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$snapshotRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'TestBuilds'))
# An explicitly selected candidate is resolved before the ready pointer.
if ($Candidate) {
    $directory = [IO.Path]::GetFullPath((Join-Path $snapshotRoot $Candidate))
} else {
    $latest = Join-Path $snapshotRoot 'LatestReady.json'
    if (!(Test-Path -LiteralPath $latest)) { throw 'No verified Veehiicuul3 snapshot is ready. Use an explicitly named candidate until a verified build is published.' }
    $information = Get-Content -LiteralPath $latest -Raw | ConvertFrom-Json
    $directory = [IO.Path]::GetFullPath((Join-Path $snapshotRoot $information.Directory))
}
if (!$directory.StartsWith(($snapshotRoot + [IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)) { throw 'Snapshot path is outside TestBuilds.' }
$manifest = Get-Content -LiteralPath (Join-Path $directory 'BuildInfo.json') -Raw | ConvertFrom-Json
if (!$Candidate -and !$manifest.Ready) { throw 'LatestReady selected an unverified snapshot.' }
$executable = Join-Path $directory 'Veehiicuul3.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Snapshot executable is missing.' }
if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -ne $manifest.ExecutableSha256) { throw 'Snapshot executable hash does not match its immutable manifest.' }
$runArguments = @()
if ($TestMode -ne 'None') {
    if ($Monitor -and $Monitor -ne 'NE18NZ2') { throw 'Assistant testing is restricted to NE18NZ2.' }
    $runArguments += @($(if ($TestMode -eq 'Hidden') { '--hidden-test' } else { '--smoke-test' }), '--test-monitor','NE18NZ2')
} elseif ($Monitor) {
    $runArguments += @('--monitor', ('"' + $Monitor.Replace('"','') + '"'))
}
$launch = @{ FilePath=$executable; WorkingDirectory=$directory; PassThru=$true }
if ($LogDirectory) { $runArguments += @('--log-directory',('"' + $LogDirectory.Replace('"','') + '"')) }
if ($runArguments.Count) { $launch.ArgumentList=$runArguments }
if ($TestMode -eq 'Hidden') { $launch.WindowStyle='Hidden' }
$process = Start-Process @launch
if ($TestMode -ne 'None') {
    if (!$process.WaitForExit(45000)) { Stop-Process -Id $process.Id; throw 'Owned test process exceeded 45 seconds and was stopped.' }
} elseif ($Wait) { $process.WaitForExit() }
if ($Wait -or $TestMode -ne 'None') { if ($process.ExitCode -ne 0) { Write-Error "Veehiicuul3 returned $($process.ExitCode). See Veehiicuul3/LogOutput diagnostics." -ErrorAction Continue; exit $process.ExitCode } }
