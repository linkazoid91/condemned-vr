<#
.SYNOPSIS
    Prepares an isolated Condemned desktop stage without any ASI loader.

.DESCRIPTION
    Copies the small root runtime files into a project-local, Git-ignored stage,
    deliberately excluding retail d3d9.dll and the scripts directory. The large
    stock Game directory is exposed through a junction and remains unmodified.
    A project-local user directory is used by the launcher.

    The retail installation is read only. This script refuses to reuse a
    non-empty stock stage so stale proxy or ASI files cannot be inherited.

.PARAMETER RetailRoot
    Optional Condemned installation root. Steam libraries are searched when
    omitted.
#>
[CmdletBinding()]
param(
    [string]$RetailRoot
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$installation = Resolve-CondemnedRetailInstallation $RetailRoot
$retail = $installation.RetailRoot

$baselinePath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\retail-manifest.json')
if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    throw 'M0 retail manifest missing. Run tools\verify-condemned-m0.ps1 first.'
}
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
if (-not $baseline.VerifiedBuild) {
    throw 'The captured M0 retail manifest does not describe a verified build.'
}
if ([IO.Path]::GetFullPath($baseline.RetailRoot) -ne
    [IO.Path]::GetFullPath($retail)) {
    throw 'The M0 manifest belongs to a different retail installation.'
}

foreach ($relativePath in $cfg.CriticalFiles.Keys) {
    $actual = Get-FileSha256 (Join-Path $retail $relativePath)
    if ($actual -ne $cfg.CriticalFiles[$relativePath].Sha256) {
        throw "Critical retail identity changed since capture: $relativePath"
    }
}

$stageRoot = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi')
$userDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\userdata-stock-no-asi')
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi-deployment.json')

if (Test-Path -LiteralPath $stageRoot) {
    $existing = @(Get-ChildItem -LiteralPath $stageRoot -Force)
    if ($existing.Count -gt 0) {
        throw ("Stock stage is not empty and will not be reused: $stageRoot. " +
            'Remove this generated stage explicitly before preparing it again.')
    }
} else {
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
}
New-Item -ItemType Directory -Force -Path $userDirectory | Out-Null

$excludedRootFiles = @('d3d9.dll')
$copiedFiles = New-Object Collections.Generic.List[object]
foreach ($source in @(
        Get-ChildItem -LiteralPath $retail -File -Force |
            Sort-Object Name
    )) {
    if ($excludedRootFiles -icontains $source.Name) {
        continue
    }
    $destination = Join-Path $stageRoot $source.Name
    [IO.File]::Copy($source.FullName, $destination, $false)
    $sourceHash = Get-FileSha256 $source.FullName
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw "Staged copy failed hash verification: $($source.Name)"
    }
    $copiedFiles.Add([pscustomobject][ordered]@{
        Name = $source.Name
        Length = $source.Length
        Sha256 = $destinationHash
    })
}

$archiveConfig = Join-Path $stageRoot 'default.archcfg'
$archiveLines = @(
    [IO.File]::ReadAllLines($archiveConfig) |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
$expectedArchiveLines = @(
    'Game',
    'Game\CondemnedA.Arch00',
    'Game\CondemnedL.Arch00'
)
if ($archiveLines.Count -ne $expectedArchiveLines.Count) {
    throw 'Unexpected entry count in staged default.archcfg.'
}
for ($i = 0; $i -lt $expectedArchiveLines.Count; $i++) {
    if ($archiveLines[$i] -cne $expectedArchiveLines[$i]) {
        throw ("Unexpected staged archive entry '$($archiveLines[$i])'; " +
            "expected '$($expectedArchiveLines[$i])'.")
    }
}

$retailGame = Join-Path $retail 'Game'
$stageGame = Join-Path $stageRoot 'Game'
New-Item -ItemType Junction -Path $stageGame -Target $retailGame | Out-Null
$gameItem = Get-Item -LiteralPath $stageGame -Force
if (-not ($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'The staged Game path is not a junction.'
}

foreach ($language in @('English', 'French', 'German', 'Italian', 'Spanish')) {
    New-Item -ItemType Directory -Path (Join-Path $stageRoot $language) |
        Out-Null
}

foreach ($forbidden in @('d3d9.dll', 'scripts')) {
    if (Test-Path -LiteralPath (Join-Path $stageRoot $forbidden)) {
        throw "Forbidden external-fix component entered the stock stage: $forbidden"
    }
}

$deployment = [pscustomobject][ordered]@{
    SchemaVersion = 1
    PreparedAtUtc = [DateTime]::UtcNow.ToString('o')
    RetailRoot = $retail
    RetailManifest = $baselinePath
    StageRoot = $stageRoot
    RuntimeExe = Join-Path $stageRoot 'Condemned.exe'
    UserDirectory = $userDirectory
    ArchiveConfig = $archiveConfig
    ArchiveConfigSha256 = Get-FileSha256 $archiveConfig
    GameJunction = [pscustomobject][ordered]@{
        Path = $stageGame
        Target = [IO.Path]::GetFullPath($retailGame)
    }
    Excluded = @(
        'd3d9.dll',
        'scripts\Condemned.WidescreenFix.asi',
        'scripts\Condemned.WidescreenFix.ini'
    )
    CopiedFiles = $copiedFiles
}
$json = $deployment | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText(
    $deploymentPath,
    $json + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host '=== Condemned M0 stock/no-ASI stage prepared ===' `
    -ForegroundColor Green
Write-Host "Stage:      $stageRoot"
Write-Host "Game data:  $retailGame (junction; stock archives selected)"
Write-Host "User data:  $userDirectory"
Write-Host "Deployment: $deploymentPath"
Write-Host 'Excluded:   d3d9.dll and scripts (no Ultimate ASI Loader/Widescreen Fix)'
