param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Test,
    [switch]$ConsoleTests,
    [switch]$SkipDisplayVerification
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) { throw 'No Visual Studio installation with x64 C++ tools was found.' }
$developerScript = Join-Path $visualStudio 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerScript -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$ninja = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach ($tool in @($cmake, $ctest, $ninja)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool. Install Visual Studio's C++ CMake tools component." }
}
$buildDirectory = Join-Path $PSScriptRoot "BuildOutput\$Configuration"
$cache = Join-Path $buildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $sourceEntry = Get-Content -LiteralPath $cache | Where-Object { $_.StartsWith('CMAKE_HOME_DIRECTORY:INTERNAL=') } | Select-Object -First 1
    if ($sourceEntry) {
        $previousSource = [IO.Path]::GetFullPath($sourceEntry.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length))
        if (!$previousSource.Equals([IO.Path]::GetFullPath($PSScriptRoot),[StringComparison]::OrdinalIgnoreCase)) {
            $generatedRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'BuildOutput')) + [IO.Path]::DirectorySeparatorChar
            $preserved = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('BuildOutput\BeforeRootMove_' + $Configuration + '_' + [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff'))))
            if (![IO.Path]::GetFullPath($buildDirectory).StartsWith($generatedRoot) -or !$preserved.StartsWith($generatedRoot) -or (Test-Path -LiteralPath $preserved)) { throw 'Unsafe build cache archive path.' }
            Move-Item -LiteralPath $buildDirectory -Destination $preserved
            Write-Host 'Preserved old generated output; configuring from the current project location.'
        }
    }
}
& $cmake -S $PSScriptRoot -B $buildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $buildDirectory
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    if ($Test -or $ConsoleTests) {
        $testArguments = @('--test-dir',$buildDirectory,'--output-on-failure')
        if ($ConsoleTests) { $testArguments += @('-LE','VisibleDesktop') }
        if ($SkipDisplayVerification) { $testArguments += @('-E','^DisplayEtw(Hardware|Warp)$') }
        & $ctest @testArguments
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
Write-Host "Built $buildDirectory\Veehiicuul3.exe"

