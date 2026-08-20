<#
.SYNOPSIS
    Configures, builds and tests both Condemned VR architectures.

.DESCRIPTION
    Verifies pinned dependencies, configures isolated x86 and x64 CMake build
    trees, builds project-authored targets, runs CTest and writes a local
    manifest containing artifact hashes and tool/source state.

    The script does not read, copy or modify the retail game installation.

.PARAMETER Configuration
    CMake build configuration. Defaults to RelWithDebInfo.

.PARAMETER Clean
    Deletes only the two validated project-local Condemned build directories
    before configuring them again.

.PARAMETER SkipTests
    Builds without running CTest. Do not use for an acceptance build.
#>
[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Debug', 'Release')]
    [string]$Configuration = 'RelWithDebInfo',

    [switch]$Clean,

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

function Get-ToolPath {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Fallbacks
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "$Name was not found on PATH or at a known installation path."
}

$cmake = Get-ToolPath -Name 'cmake' -Fallbacks @(
    'C:\Program Files\CMake\bin\cmake.exe'
)
$ctest = Get-ToolPath -Name 'ctest' -Fallbacks @(
    'C:\Program Files\CMake\bin\ctest.exe'
)
$generator = 'Visual Studio 17 2022'

$targets = @(
    [ordered]@{
        Name = 'condemned-x86-vs'
        Platform = 'Win32'
        Options = @(
            '-DCONDEMNEDVR_BUILD_M1_LOADER=ON',
            '-DCONDEMNEDVR_BUILD_M2_BRIDGE=ON',
            '-DCONDEMNEDVR_BUILD_HOST=OFF'
        )
    },
    [ordered]@{
        Name = 'condemned-x64-vs'
        Platform = 'x64'
        Options = @(
            '-DCONDEMNEDVR_BUILD_M1_LOADER=OFF',
            '-DCONDEMNEDVR_BUILD_M2_BRIDGE=OFF',
            '-DCONDEMNEDVR_BUILD_HOST=ON'
        )
    }
)

Write-Host "=== Condemned VR build ($Configuration) ===" -ForegroundColor Cyan

# Called scripts throw on failure. PowerShell does not reliably refresh
# LASTEXITCODE when invoking another .ps1 file.
& "$PSScriptRoot\prepare-dependencies.ps1" -VerifyOnly

$results = [ordered]@{}
foreach ($target in $targets) {
    $buildDir = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $cfg.ProjectRoot "build\$($target.Name)")

    if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
        Write-Host "--- $($target.Name): removing validated build tree ---"
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }

    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cachedGenerator = Get-Content -LiteralPath $cachePath |
            Where-Object { $_ -like 'CMAKE_GENERATOR:INTERNAL=*' } |
            Select-Object -First 1
        if ($cachedGenerator -and
            $cachedGenerator -ne "CMAKE_GENERATOR:INTERNAL=$generator") {
            throw ("$buildDir uses a different CMake generator. " +
                "Run this script once with -Clean.")
        }
    }

    Write-Host "--- $($target.Name): configure ---" -ForegroundColor Cyan
    & $cmake -S $cfg.ProjectRoot -B $buildDir `
        -G $generator -A $target.Platform `
        @($target.Options) '-DCONDEMNEDVR_BUILD_TESTS=ON'
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed for $($target.Name)."
    }

    Write-Host "--- $($target.Name): build ---" -ForegroundColor Cyan
    & $cmake --build $buildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $($target.Name)."
    }

    $testSummary = 'skipped'
    if (-not $SkipTests) {
        Write-Host "--- $($target.Name): tests ---" -ForegroundColor Cyan
        & $ctest --test-dir $buildDir -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "CTest failed for $($target.Name)."
        }
        $testSummary = 'passed'
    }

    $results[$target.Name] = [ordered]@{
        platform = $target.Platform
        buildDirectory = $buildDir
        tests = $testSummary
    }
}


if (-not $SkipTests) {
    Write-Host (
        '--- PowerShell: launch feature profile ---') `
        -ForegroundColor Cyan
    & $PSScriptRoot\test-condemned-launch-profile.ps1

    Write-Host (
        '--- PowerShell: launcher focus handoff ---') `
        -ForegroundColor Cyan
    & "$PSScriptRoot\test-condemned-window-focus.ps1"

    Write-Host (
        '--- PowerShell: game-window screenshot helper ---') `
        -ForegroundColor Cyan
    & "$PSScriptRoot\capture-condemned-window.ps1" -ValidateOnly

    Write-Host (
        '--- PowerShell: weapon diagnostics watcher ---') `
        -ForegroundColor Cyan
    & "$PSScriptRoot\test-condemned-weapon-diagnostics.ps1"
}

$artifactCandidates = [ordered]@{
    'GameClient.dll (project loader, x86)' =
        "build\condemned-x86-vs\src\condemned_gameclient_loader\$Configuration\GameClient.dll"
    'condemnedvr-d3d9.dll (bridge, x86)' =
        "build\condemned-x86-vs\src\condemned_proxy32\$Configuration\condemnedvr-d3d9.dll"
    'condemnedvr-d3d9-diagnostic.dll (x86)' =
        "build\condemned-x86-vs\src\condemned_proxy32\$Configuration\condemnedvr-d3d9-diagnostic.dll"
    'condemnedvr-host.exe (x64)' =
        "build\condemned-x64-vs\src\condemned_host64\$Configuration\condemnedvr-host.exe"
}

$artifacts = @()
foreach ($label in $artifactCandidates.Keys) {
    $path = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $cfg.ProjectRoot $artifactCandidates[$label])
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Expected build artifact is missing: $path"
    }
    $item = Get-Item -LiteralPath $path
    $artifacts += [ordered]@{
        label = $label
        path = $path
        sha256 = Get-FileSha256 $path
        bytes = $item.Length
        modifiedUtc = $item.LastWriteTimeUtc.ToString('s') + 'Z'
    }
}

$gitCommit = (& git -C $cfg.ProjectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not read the Git commit.'
}
$gitDirty = [bool](& git -C $cfg.ProjectRoot status --porcelain)
$cmakeVersion = (& $cmake --version | Select-Object -First 1)

$manifestPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-build-manifest.json')
New-Item -ItemType Directory -Force `
    -Path (Split-Path -Parent $manifestPath) | Out-Null

[ordered]@{
    builtUtc = (Get-Date).ToUniversalTime().ToString('s') + 'Z'
    configuration = $Configuration
    gitCommit = $gitCommit
    gitWorkingTreeDirty = $gitDirty
    cmake = $cmakeVersion
    targets = $results
    artifacts = @($artifacts)
} | ConvertTo-Json -Depth 6 |
    Out-File -Encoding utf8 -LiteralPath $manifestPath

Write-Host ''
Write-Host 'Build and tests completed.' -ForegroundColor Green
if ($gitDirty) {
    Write-Host ('The working tree is dirty; the manifest is not bound to an ' +
        'exact source state.') -ForegroundColor Yellow
}
foreach ($artifact in $artifacts) {
    Write-Host ("  {0,-43} {1}" -f
        $artifact.label, $artifact.sha256.Substring(0, 16))
}
Write-Host "Manifest: $manifestPath"
