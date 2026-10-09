param([switch]$VerifyVisible)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ((git -C $PSScriptRoot status --porcelain)) { throw 'Package only clean committed source.' }
$commit = (git -C $PSScriptRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
$short = $commit.Substring(0,12)
$stamp = [DateTime]::UtcNow.ToString('yyyy-MM-dd_HH-mm-ss-fff')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'TestBuilds'))
$candidate = [IO.Path]::GetFullPath((Join-Path $root "Candidate_${stamp}_${short}"))
$verified = [IO.Path]::GetFullPath((Join-Path $root "${stamp}_${short}"))
foreach ($path in @($candidate,$verified)) {
    if (!$path.StartsWith(($root + [IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $path)) { throw 'Unsafe or existing snapshot path.' }
}
$release = Join-Path $PSScriptRoot 'BuildOutput\Release'
$debug = Join-Path $PSScriptRoot 'BuildOutput\Debug'
if ($VerifyVisible) { & (Join-Path $PSScriptRoot 'Build.ps1') -Test } else { & (Join-Path $PSScriptRoot 'Build.ps1') -ConsoleTests }
& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration Debug -ConsoleTests
New-Item -ItemType Directory -Path $candidate,(Join-Path $candidate 'Verification'),(Join-Path $candidate 'Examples') -Force | Out-Null
$ship = @('Veehiicuul3.exe','GameInputBridge.dll','GameInputRawInputProxy.exe','GameInputRedist.dll','GameInputLicense.txt','GameInputNotice.txt','PresentMonLicense.txt','PresentMonProvenance.md')
foreach ($file in $ship) { Copy-Item -LiteralPath (Join-Path $release $file) -Destination $candidate }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Run.cmd'),(Join-Path $PSScriptRoot 'Run.ps1') -Destination $candidate
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Source\Assets\SlopeCar.modeler') -Destination (Join-Path $candidate 'Examples')
Copy-Item -LiteralPath (Join-Path $release 'Testing\Temporary\LastTest.log') -Destination (Join-Path $candidate 'Verification\ReleaseCTest.log')
Copy-Item -LiteralPath (Join-Path $debug 'Testing\Temporary\LastTest.log') -Destination (Join-Path $candidate 'Verification\DebugCTest.log')
$info = [ordered]@{Project='Veehiicuul3';Commit=$commit;Ready=$false;Status='Candidate';ExecutableSha256=(Get-FileHash -LiteralPath (Join-Path $candidate 'Veehiicuul3.exe') -Algorithm SHA256).Hash;CreatedUtc=[DateTime]::UtcNow.ToString('o');Verification=[ordered]@{ReleaseNoninteractive=$true;DebugNoninteractive=$true;CopiedHardware=$false;CopiedWarp=$false;CopiedVisibleHardware=$false;CopiedVisibleWarp=$false;PhysicalMouse=$false;PhysicalGamepad=$false;ActualEtwDisplayLatency=$false}}
$manifest = Join-Path $candidate 'BuildInfo.json'
$info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding utf8
$unrelated = Join-Path ([IO.Path]::GetTempPath()) ("Veehiicuul3_CopyProbe_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $unrelated | Out-Null
function CopyProbe([bool]$Visible,[bool]$Warp,[string]$Name) {
    $logs = Join-Path $candidate ("Verification\" + $Name)
    $arguments = @($(if ($Visible) { '--smoke-test' } else { '--hidden-test' }),'--test-monitor','NE18NZ2','--log-directory',('"' + $logs + '"'))
    if ($Warp) { $arguments += '--software' }
    $launch = @{FilePath=(Join-Path $candidate 'Veehiicuul3.exe');WorkingDirectory=$unrelated;ArgumentList=$arguments;PassThru=$true}
    if (!$Visible) { $launch.WindowStyle='Hidden' }
    $process = Start-Process @launch
    if (!$process.WaitForExit(45000)) { Stop-Process -Id $process.Id; throw "Copied $Name timed out; its own process was stopped." }
    if ($process.ExitCode -eq 77 -and $Visible) { return $false }
    if ($process.ExitCode -ne 0) { throw "Copied $Name failed with exit $($process.ExitCode). Candidate retained: $candidate" }
    return $true
}
$info.Verification.CopiedHardware = CopyProbe $false $false 'CopyHardware'
$info.Verification.CopiedWarp = CopyProbe $false $true 'CopyWarp'
if ($VerifyVisible) {
    $info.Verification.CopiedVisibleHardware = CopyProbe $true $false 'CopyVisibleHardware'
    if ($info.Verification.CopiedVisibleHardware) { $info.Verification.CopiedVisibleWarp = CopyProbe $true $true 'CopyVisibleWarp' }
}
$exampleSession = Get-ChildItem -LiteralPath (Join-Path $candidate 'Verification\CopyHardware') -Directory | Select-Object -First 1
foreach ($file in @('TwoPaint.modeler','TwoPaint.h','ExampleCircuit.track','OutlineColors.track','OutlineDegree.track')) { Copy-Item -LiteralPath (Join-Path $exampleSession.FullName $file) -Destination (Join-Path $candidate 'Examples') }
# Actual selector and snapshot launchers run from an unrelated working directory.
Push-Location $unrelated
try {
    & (Join-Path $PSScriptRoot 'RunLatestBuild.ps1') -Candidate (Split-Path $candidate -Leaf) -TestMode Hidden -Wait
    & (Join-Path $candidate 'Run.ps1') -TestMode Hidden -LogDirectory (Join-Path $candidate 'Verification\SnapshotLauncher')
} finally { Pop-Location }
$info.Ready = $info.Verification.CopiedVisibleHardware -and $info.Verification.CopiedVisibleWarp
$info.Status = if ($info.Ready) { 'Verified first runnable milestone' } else { 'Candidate - displayed checks pending' }
$info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding utf8
@"
# Veehiicuul3 - $($info.Status)

Source commit: $commit
Executable SHA-256: $($info.ExecutableSha256)

Run.cmd launches this self-contained copy visibly on Windows' primary monitor.
Run.cmd -Monitor NE18NZ2 selects that monitor. Displays must be at least 2560x1440 and exactly 100% scaling. The borderless window centers an unscaled 2560x1440 image with black margins.

Release/Debug noninteractive and copied hardware/WARP checks passed. Displayed copied verification: $($info.Ready). Verification/ contains full logs, 2560x1440 application renders and centered presentation evidence. Cursor operations in tests are simulated; no test activation/capture occurs. Physical mouse/gamepad usability and actual ETW display/input latency remain pending. This is a real first migration milestone, not complete parity. See the source README and Documentation/Parity.md.

Examples contains source SlopeCar, two-material cage/export and example track. Shipped files are read-only. Save As outside this snapshot. Development builds and later snapshots do not overwrite it.
"@ | Set-Content -LiteralPath (Join-Path $candidate 'README.md') -Encoding utf8
Add-Type -AssemblyName System.Drawing
foreach ($bitmapFile in (Get-ChildItem -LiteralPath $candidate -Recurse -File -Filter '*.bmp')) {
    $bitmap = [System.Drawing.Bitmap]::new($bitmapFile.FullName)
    try { $bitmap.Save([IO.Path]::ChangeExtension($bitmapFile.FullName,'.png'),[System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    Remove-Item -LiteralPath $bitmapFile.FullName
}
if ($info.Ready) { Move-Item -LiteralPath $candidate -Destination $verified; $directory=$verified } else { $directory=$candidate }
$files = Get-ChildItem -LiteralPath $directory -File -Recurse | ForEach-Object { [ordered]@{Path=[IO.Path]::GetRelativePath($directory,$_.FullName);Bytes=$_.Length;Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} }
$files | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $directory 'Files.json') -Encoding utf8
Get-ChildItem -LiteralPath $directory -File -Recurse | ForEach-Object { $_.IsReadOnly=$true }
if ($info.Ready) {
    $pointer = [ordered]@{Directory=(Split-Path $directory -Leaf);Executable='Veehiicuul3.exe';Commit=$commit;ExecutableSha256=$info.ExecutableSha256}
    $temporaryPointer = Join-Path $root ('LatestReady.' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $pointer | ConvertTo-Json | Set-Content -LiteralPath $temporaryPointer -Encoding utf8
    Move-Item -LiteralPath $temporaryPointer -Destination (Join-Path $root 'LatestReady.json') -Force
}
Write-Output "Snapshot: $directory"
Write-Output "Ready: $($info.Ready)"
