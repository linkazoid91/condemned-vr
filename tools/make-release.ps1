<#
.SYNOPSIS
    Builds a redistributable Condemned VR end-user package.

.DESCRIPTION
    Produces a folder and ZIP containing project-authored binaries, launch and
    installation tools, defaults, documentation, notices, and an integrity
    manifest. It never reads or packages Retail game files.
#>
[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string]$Configuration = 'RelWithDebInfo',
    [switch]$SkipBuild,
    [switch]$NoArchive,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-env.ps1')
$cfg = Get-CondemnedVrConfig

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-all.ps1') -Configuration $Configuration
}

$cmakeText = Get-Content -Raw -LiteralPath (
    Join-Path $cfg.ProjectRoot 'CMakeLists.txt')
if ($cmakeText -notmatch
    '(?ms)project\(CondemnedVr\s+VERSION\s+(\d+\.\d+\.\d+)') {
    throw 'CMakeLists.txt does not declare the CondemnedVr version.'
}
$version = $Matches[1]
if ($cmakeText -match
    '(?m)^\s*set\(FEARVR_VERSION_LABEL\s+"([^"]*)"\)') {
    if (-not [string]::IsNullOrWhiteSpace($Matches[1])) {
        $version += '-' + $Matches[1]
    }
}
$gitCommit = (& git -C $cfg.ProjectRoot rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not read the Git commit.'
}
$gitDirty = [bool](& git -C $cfg.ProjectRoot status --porcelain)
$version += '+' + $gitCommit
if ($gitDirty) {
    Write-Host 'WARNING: packaging a dirty tree; not publishable.' -ForegroundColor Yellow
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $cfg.ProjectRoot 'dist'
}
$OutputRoot = Assert-UnderCondemnedVrProjectRoot $OutputRoot
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$packageName = 'condemned-vr-' + $version
$packageRoot = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $OutputRoot $packageName)
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
foreach ($relative in @('bin\x86', 'bin\x64', 'tools', 'images', 'docs')) {
    New-Item -ItemType Directory -Force -Path (
        Join-Path $packageRoot $relative) | Out-Null
}

$files = [ordered]@{
    'bin\x86\GameClient.dll' =
        ('build\condemned-x86-vs\src\condemned_gameclient_loader\{0}\GameClient.dll' -f
            $Configuration)
    'bin\x86\condemnedvr-d3d9.dll' =
        ('build\condemned-x86-vs\src\condemned_proxy32\{0}\condemnedvr-d3d9.dll' -f
            $Configuration)
    'bin\x86\condemnedvr-defaults.ini' =
        'config\condemnedvr-defaults.ini'
    'bin\x64\condemnedvr-host.exe' =
        ('build\condemned-x64-vs\src\condemned_host64\{0}\condemnedvr-host.exe' -f
            $Configuration)
    'tools\_condemnedvr-env.ps1' = 'tools\_condemnedvr-env.ps1'
    'tools\_condemnedvr-release.ps1' =
        'tools\release\_condemnedvr-release.ps1'
    'tools\_condemnedvr-launch-profile.ps1' =
        'tools\_condemnedvr-launch-profile.ps1'
    'tools\_condemnedvr-window-focus.ps1' =
        'tools\_condemnedvr-window-focus.ps1'
    'tools\install.ps1' = 'tools\release\install.ps1'
    'tools\play.ps1' = 'tools\release\play.ps1'
    'tools\uninstall.ps1' = 'tools\release\uninstall.ps1'
    'tools\launch-condemned-m2-vr.ps1' =
        'tools\launch-condemned-m2-vr.ps1'
    'tools\inspect-condemned-process32.ps1' =
        'tools\inspect-condemned-process32.ps1'
    'tools\watch-condemned-weapon-diagnostics.ps1' =
        'tools\watch-condemned-weapon-diagnostics.ps1'
    'tools\watch-condemned-performance.ps1' =
        'tools\watch-condemned-performance.ps1'
    'Install.cmd' = 'tools\release\Install.cmd'
    'Play.cmd' = 'tools\release\Play.cmd'
    'Uninstall.cmd' = 'tools\release\Uninstall.cmd'
    'images\title.png' = 'images\title.png'
    'README.md' = 'tools\release\README-PACKAGE.md'
    'LICENSE' = 'LICENSE'
    'ATTRIBUTION.md' = 'ATTRIBUTION.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'docs\CURRENT_STATE.md' = 'docs\CURRENT_STATE.md'
    'docs\TESTING.md' = 'docs\TESTING.md'
}

foreach ($target in $files.Keys) {
    $source = Join-Path $cfg.ProjectRoot $files[$target]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw ('Release input is missing: {0}' -f $source)
    }
    $destination = Join-Path $packageRoot $target
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$forbiddenNames = @(
    'Condemned.exe', 'GameOrig.dll', 'GameServer.dll', 'ClientFx.fxd')
foreach ($item in Get-ChildItem -LiteralPath $packageRoot -Recurse -File) {
    if ($forbiddenNames -icontains $item.Name) {
        throw ('Safety stop: Retail file entered the release: {0}' -f
            $item.FullName)
    }
}

$manifestFiles = foreach ($item in (
        Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
            Sort-Object FullName
    )) {
    $relative = $item.FullName.Substring($packageRoot.Length + 1)
    [ordered]@{
        path = $relative
        sha256 = Get-FileSha256 $item.FullName
        bytes = $item.Length
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    product = 'Condemned VR'
    version = $version
    gitCommit = $gitCommit
    gitWorkingTreeDirty = $gitDirty
    builtUtc = [DateTime]::UtcNow.ToString('o')
    configuration = $Configuration
    containsRetailContent = $false
    files = @($manifestFiles)
}
$manifestPath = Join-Path $packageRoot 'release-manifest.json'
[IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

. (Join-Path $cfg.ProjectRoot 'tools\release\_condemnedvr-release.ps1')
$verified = Test-CondemnedVrReleasePackage $packageRoot

$archivePath = $packageRoot + '.zip'
if (-not $NoArchive) {
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath (
        $archivePath)
}

Write-Host 'Condemned VR release package created.' -ForegroundColor Green
Write-Host ('Folder:  {0}' -f $packageRoot)
if (-not $NoArchive) {
    Write-Host ('Archive: {0}' -f $archivePath)
}
Write-Host ('Files:   {0}' -f @($verified.files).Count)
