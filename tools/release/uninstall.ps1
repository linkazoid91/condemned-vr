<#
.SYNOPSIS
    Safely removes an isolated Condemned VR installation.

.DESCRIPTION
    Without -Apply this is a dry run. Userdata and LocalAppData weapon
    settings are preserved unless -IncludeUserData is supplied.
#>
[CmdletBinding()]
param(
    [string]$InstallDir,
    [switch]$IncludeUserData,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-release.ps1')
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $selfRoot = Get-NormalizedDirectoryPath (Join-Path $PSScriptRoot '..')
    $selfManifest = Join-Path $selfRoot 'condemnedvr-install.json'
    if (Test-Path -LiteralPath $selfManifest -PathType Leaf) {
        $InstallDir = $selfRoot
    } else {
        $InstallDir = Join-Path $env:USERPROFILE 'CondemnedVR'
    }
}
$InstallDir = Get-NormalizedDirectoryPath $InstallDir
$mode = if ($Apply) { 'APPLY' } else { 'DRY RUN' }
Write-Host ('=== Condemned VR - Uninstall ({0}) ===' -f $mode) -ForegroundColor Cyan

if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) {
    Write-Host ('No installation exists in: {0}' -f $InstallDir)
    return
}
$install = Read-CondemnedVrInstallManifest $InstallDir
$retailBefore = Assert-CondemnedRetailReleaseIdentity $install.retailRoot

if ($Apply) {
    $runningGame = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
    $runningHost = @(
        Get-Process -Name 'condemnedvr-host' -ErrorAction SilentlyContinue)
    if ($runningGame.Count -gt 0 -or $runningHost.Count -gt 0) {
        throw 'Close Condemned and the Condemned VR host before uninstalling.'
    }
}

$keepNames = @()
if (-not $IncludeUserData) {
    $keepNames = @('userdata', 'condemnedvr-install.json')
}
foreach ($entry in @(
        Get-ChildItem -LiteralPath $InstallDir -Force
    )) {
    if ($keepNames -icontains $entry.Name) {
        Write-Host ('  keep   {0}' -f $entry.Name)
    } else {
        Write-Host ('  remove {0}' -f $entry.Name)
    }
}

$userSettings = Join-Path $env:LOCALAPPDATA 'CondemnedVR'
if ($IncludeUserData -and
    (Test-Path -LiteralPath $userSettings -PathType Container)) {
    Write-Host ('  remove {0}' -f $userSettings)
}

if (-not $Apply) {
    Write-Host ''
    Write-Host 'Nothing was changed. Re-run with -Apply to remove it.' -ForegroundColor Yellow
    return
}

# Remove the verified junction itself before recursively removing its parent.
$stageDeploymentPath = Resolve-SafeRelativePath $InstallDir (
    [string]$install.stageDeployment)
if (Test-Path -LiteralPath $stageDeploymentPath -PathType Leaf) {
    $stage = Get-Content -Raw -LiteralPath $stageDeploymentPath |
        ConvertFrom-Json
    $stageGame = Join-Path $stage.WorkingDirectory 'Game'
    if (Test-Path -LiteralPath $stageGame) {
        $gameItem = Get-Item -LiteralPath $stageGame -Force
        if (-not ($gameItem.Attributes -band
                [IO.FileAttributes]::ReparsePoint)) {
            throw 'Safety stop: the installed Game path is not a junction.'
        }
        $target = [string](@($gameItem.Target)[0])
        $expectedTarget = Join-Path $install.retailRoot 'Game'
        if (-not (Get-NormalizedDirectoryPath $target).Equals(
                (Get-NormalizedDirectoryPath $expectedTarget),
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Safety stop: the installed Game junction target changed.'
        }
        [IO.Directory]::Delete($stageGame)
    }
}

foreach ($entry in @(
        Get-ChildItem -LiteralPath $InstallDir -Force
    )) {
    if ($keepNames -icontains $entry.Name) {
        continue
    }
    Remove-Item -LiteralPath $entry.FullName -Recurse -Force
}
if ($IncludeUserData) {
    if (Test-Path -LiteralPath $userSettings -PathType Container) {
        $expectedSettings = Get-NormalizedDirectoryPath (
            Join-Path $env:LOCALAPPDATA 'CondemnedVR')
        if (-not (Get-NormalizedDirectoryPath $userSettings).Equals(
                $expectedSettings,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Safety stop: unexpected LocalAppData settings path.'
        }
        Remove-Item -LiteralPath $userSettings -Recurse -Force
    }
    if (Test-Path -LiteralPath $InstallDir -PathType Container) {
        $remaining = @(Get-ChildItem -LiteralPath $InstallDir -Force)
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $InstallDir -Force
        }
    }
}

$shortcut = [string]$install.shortcut
if (-not [string]::IsNullOrWhiteSpace($shortcut) -and
    (Test-Path -LiteralPath $shortcut -PathType Leaf)) {
    $arguments = ''
    try {
        $arguments = (New-Object -ComObject WScript.Shell).
            CreateShortcut($shortcut).Arguments
    } catch { }
    if ($arguments -like ('*{0}*' -f $InstallDir)) {
        Remove-Item -LiteralPath $shortcut -Force
    }
}

$retailAfter = Assert-CondemnedRetailReleaseIdentity $install.retailRoot
if ($retailAfter.RuntimeSha256 -ne $retailBefore.RuntimeSha256) {
    throw 'Safety stop: the Retail executable changed during uninstall.'
}
Write-Host ''
Write-Host 'Uninstall complete. The Steam installation is unchanged.' -ForegroundColor Green
if (-not $IncludeUserData) {
    Write-Host 'Userdata and LocalAppData weapon settings were preserved.'
}
