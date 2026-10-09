Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Scripts\BuildDelivery.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('Veehiicuul3_DeliveryTest_'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$pointer=Join-Path $root 'LatestReady.json'
$original='{"Directory":"previous-fully-verified","Commit":"old"}'
Set-Content -LiteralPath $pointer -Value $original -Encoding utf8 -NoNewline
$before=(Get-FileHash -LiteralPath $pointer).Hash
foreach($wrapper in @('Run','RunLatestBuild')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\$wrapper.cmd") -Destination $root
    'param([string]$TestMode); Set-StrictMode -Version Latest; $ErrorActionPreference="Stop"; [Console]::Error.WriteLine("Forced launcher failure 23"); exit 23' | Set-Content -LiteralPath (Join-Path $root "$wrapper.ps1") -Encoding utf8
    $script:finalized=$false; $failed=$false
    try {
        Complete-BuildDelivery -Root $root -LauncherGate {
            Invoke-CmdBuildGate -Script (Join-Path $root "$wrapper.cmd") -Arguments @('-TestMode','Smoke') -WorkingDirectory ([IO.Path]::GetTempPath()) -EvidenceDirectory (Join-Path $root $wrapper) -TimeoutSeconds 4
        } -FinalizeSnapshot { $script:finalized=$true; return @{Directory='invalid-candidate'} }
    } catch {
        if($_.Exception.Data['ExitCode'] -ne 23) { throw }
        $failed=$true
    }
    if(!$failed -or $script:finalized -or (Get-FileHash -LiteralPath $pointer).Hash -ne $before) { throw 'Failed launcher changed publication state.' }
    $out=Get-Content -LiteralPath (Join-Path $root "$wrapper\Stdout.txt") -Raw
    if($out -match 'Press any key') { throw 'Automated wrapper paused after failure.' }
    $err=Get-Content -LiteralPath (Join-Path $root "$wrapper\Stderr.txt") -Raw
    if($err -notmatch 'Forced launcher failure 23') { throw 'Failure diagnostics were hidden.' }
}
Complete-BuildDelivery -Root $root -LauncherGate {} -FinalizeSnapshot { return @{Directory='verified-candidate';Commit='new'} }
if((Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json).Directory -ne 'verified-candidate') { throw 'Successful final promotion failed.' }
Write-Output "Forced actual CMD failure preserves the prior pointer, skips readiness/finalization, retains exit 23 and stderr, and does not pause; both wrappers. Success promotes last. Evidence: $root"
