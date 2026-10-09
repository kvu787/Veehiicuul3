Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Invoke-CmdBuildGate {
    param([string]$Script,[string[]]$Arguments,[string]$WorkingDirectory,[string]$EvidenceDirectory,[int]$TimeoutSeconds=50)
    New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
    $start=[Diagnostics.ProcessStartInfo]::new('cmd.exe')
    # Known launcher paths and generated arguments; reject cmd metacharacters.
    foreach($argument in @($Script)+$Arguments) { if($argument -match '["&|<>^\r\n]') { throw 'Unsafe CMD gate argument.' } }
    $quoted=@($Script)+$Arguments | ForEach-Object {'"'+$_+'"'}
    $start.Arguments='/d /c "'+($quoted -join ' ')+'"'
    $start.WorkingDirectory=$WorkingDirectory; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true; $start.RedirectStandardInput=$true
    $process=[Diagnostics.Process]::Start($start)
    try {
        $process.StandardInput.Close()
        $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
        if(!$process.WaitForExit($TimeoutSeconds*1000)) {
            $process.Kill($true); $process.WaitForExit()
            throw "Owned CMD gate timed out after $TimeoutSeconds seconds."
        }
        $stdout.Result | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'Stdout.txt') -Encoding utf8
        $stderr.Result | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'Stderr.txt') -Encoding utf8
        [ordered]@{Script=$Script;Arguments=$Arguments;WorkingDirectory=$WorkingDirectory;LauncherPid=$process.Id;ExitCode=$process.ExitCode} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'Launcher.json') -Encoding utf8
        if($process.ExitCode -ne 0) {
            $failure=[InvalidOperationException]::new("Actual CMD gate failed with exit $($process.ExitCode); evidence: $EvidenceDirectory")
            $failure.Data['ExitCode']=$process.ExitCode; throw $failure
        }
    } finally { $process.Dispose() }
}

function Complete-BuildDelivery {
    param([string]$Root,[scriptblock]$LauncherGate,[scriptblock]$FinalizeSnapshot)
    # No snapshot readiness or pointer mutation is allowed before this returns.
    & $LauncherGate
    $pointer=& $FinalizeSnapshot
    $temporary=Join-Path $Root ('LatestReady.'+[Guid]::NewGuid().ToString('N')+'.tmp')
    $pointer | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding utf8
    # Promotion is the final operation. A failed gate leaves the old bytes intact.
    Move-Item -LiteralPath $temporary -Destination (Join-Path $Root 'LatestReady.json') -Force
}
