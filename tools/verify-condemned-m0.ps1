<#
.SYNOPSIS
    Verifies the local Condemned 1.0.314.0 baseline and records an M0 manifest.

.DESCRIPTION
    Reads the Steam app manifest and the Condemned retail tree, verifies all
    known executable/module identities, reports the installed Ultimate ASI
    Loader and Widescreen Fix configuration, and writes a complete file
    inventory below the project root.

    No file in the retail installation is created, changed, renamed, or
    deleted. By default every installed file is hashed. Use -Quick for an
    identity-only check that does not write a manifest.

.PARAMETER RetailRoot
    Optional Condemned installation root. Steam libraries are searched when
    omitted.

.PARAMETER OutputPath
    Project-local output path for the complete JSON manifest.

.PARAMETER Quick
    Verify critical files and report third-party components without hashing
    the complete retail tree or writing a manifest.
#>
[CmdletBinding()]
param(
    [string]$RetailRoot,
    [string]$OutputPath,
    [switch]$Quick
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

if ($Quick -and -not [string]::IsNullOrWhiteSpace($OutputPath)) {
    throw '-OutputPath cannot be combined with -Quick.'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $cfg.ProjectRoot (
        'stage\condemned-m0\retail-manifest.json')
}

$installation = Resolve-CondemnedRetailInstallation $RetailRoot
$root = $installation.RetailRoot
$allOk = $true

function Write-Check([string]$Label, [string]$Value, [bool]$Good) {
    $mark = if ($Good) { '[ OK ]' } else { '[FAIL]' }
    $color = if ($Good) { 'Green' } else { 'Red' }
    Write-Host ("{0} {1,-32} {2}" -f $mark, $Label, $Value) `
        -ForegroundColor $color
    if (-not $Good) {
        $script:allOk = $false
    }
}

function Write-Info([string]$Label, [string]$Value) {
    Write-Host ("{0} {1,-32} {2}" -f '[info]', $Label, $Value)
}

Write-Host '=== Condemned VR M0 baseline verification ===' -ForegroundColor Cyan
Write-Info 'Retail root' $root
if ($null -ne $installation.AppManifest) {
    Write-Check 'Steam app ID' $installation.AppManifest.AppId (
        $installation.AppManifest.AppId -eq [string]$cfg.SteamAppId)
    Write-Info 'Steam build ID' $installation.AppManifest.BuildId
    Write-Info 'Steam app manifest' $installation.AppManifest.Path
} else {
    Write-Check 'Steam app manifest' 'not found' $false
}

$criticalResults = New-Object Collections.Generic.List[object]
foreach ($relativePath in $cfg.CriticalFiles.Keys) {
    $expected = $cfg.CriticalFiles[$relativePath]
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-Check $relativePath 'missing' $false
        $criticalResults.Add([pscustomobject][ordered]@{
            RelativePath = $relativePath
            Exists = $false
            Verified = $false
        })
        continue
    }

    $file = Get-Item -LiteralPath $path
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo(
        $file.FullName).FileVersion
    $hash = Get-FileSha256 $file.FullName
    $pe = Get-PeIdentity $file.FullName
    $checks = [ordered]@{
        Version = $version -eq $cfg.ExpectedVersion
        Sha256 = $hash -eq $expected.Sha256
    }
    foreach ($field in @(
            'Machine',
            'OptionalHeaderMagic',
            'ImageBase',
            'SizeOfImage',
            'Timestamp')) {
        if ($expected.Contains($field)) {
            $checks[$field] = $pe.$field -eq $expected[$field]
        }
    }
    $verified = @($checks.Values | Where-Object { -not $_ }).Count -eq 0

    Write-Check "$relativePath version" $version $checks.Version
    Write-Check "$relativePath SHA-256" $hash $checks.Sha256
    Write-Check "$relativePath PE identity" (
        "$($pe.Machine), image $($pe.SizeOfImage), time $($pe.Timestamp)") (
        @($checks.GetEnumerator() |
            Where-Object { $_.Key -notin @('Version', 'Sha256') -and
                -not $_.Value }).Count -eq 0)

    $criticalResults.Add([pscustomobject][ordered]@{
        RelativePath = $relativePath
        Exists = $true
        Length = $file.Length
        Version = $version
        Sha256 = $hash
        Pe = $pe
        Checks = $checks
        Verified = $verified
    })
}

Write-Host '--- Existing external fix components ---'
$thirdPartyResults = New-Object Collections.Generic.List[object]
foreach ($relativePath in $cfg.KnownThirdPartyFiles) {
    $path = Join-Path $root $relativePath
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    if (-not $exists) {
        Write-Info $relativePath 'not installed'
        $thirdPartyResults.Add([pscustomobject][ordered]@{
            RelativePath = $relativePath
            Exists = $false
        })
        continue
    }

    $file = Get-Item -LiteralPath $path
    $result = [pscustomobject][ordered]@{
        RelativePath = $relativePath
        Exists = $true
        Length = $file.Length
        Version = $file.VersionInfo.FileVersion
        Description = $file.VersionInfo.FileDescription
        Company = $file.VersionInfo.CompanyName
        Sha256 = Get-FileSha256 $file.FullName
    }
    $thirdPartyResults.Add($result)
    Write-Info $relativePath (
        "$($result.Description); SHA-256 $($result.Sha256)")
}

$widescreenIni = Join-Path $root 'scripts\Condemned.WidescreenFix.ini'
$widescreenSettings = Get-IniSettings $widescreenIni
foreach ($name in @(
        'FixAspectRatio',
        'FixMenu',
        'FixLowFramerate',
        'FixSavePath',
        'BorderlessWindowed')) {
    $value = if ($widescreenSettings.Contains($name)) {
        [string]$widescreenSettings[$name]
    } else {
        'not configured'
    }
    Write-Info "Widescreen $name" $value
}

if ($Quick) {
    Write-Host ("=== Result: {0}; quick mode, no manifest written ===" -f
        $(if ($allOk) { 'verified' } else { 'identity mismatch' })) `
        -ForegroundColor $(if ($allOk) { 'Green' } else { 'Red' })
    if (-not $allOk) {
        exit 1
    }
    exit 0
}

$running = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw ("Condemned.exe is running (PID {0}). Close it before capturing a " +
        "stable retail manifest." -f ($running.Id -join ', '))
}

$output = Assert-UnderCondemnedVrProjectRoot $OutputPath
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

Write-Host '--- Hashing complete retail tree (retail remains read-only) ---'
$fileSnapshots = @(
    Get-ChildItem -LiteralPath $root -Recurse -File -Force |
        Sort-Object FullName
)
$inventory = New-Object Collections.Generic.List[object]
$totalBytes = [uint64]0
$index = 0
foreach ($file in $fileSnapshots) {
    $index++
    $relativePath = $file.FullName.Substring($root.Length).TrimStart('\')
    Write-Progress -Activity 'Hashing Condemned retail baseline' `
        -Status "$index / $($fileSnapshots.Count): $relativePath" `
        -PercentComplete (($index * 100.0) / $fileSnapshots.Count)

    $beforeLength = $file.Length
    $beforeWrite = $file.LastWriteTimeUtc
    $hash = Get-FileSha256 $file.FullName
    $after = Get-Item -LiteralPath $file.FullName
    if ($after.Length -ne $beforeLength -or
        $after.LastWriteTimeUtc -ne $beforeWrite) {
        throw "File changed while its baseline hash was captured: $relativePath"
    }

    $category = if ($cfg.KnownThirdPartyFiles -icontains $relativePath) {
        'known-third-party'
    } else {
        'installed'
    }
    $inventory.Add([pscustomobject][ordered]@{
        RelativePath = $relativePath
        Length = [uint64]$beforeLength
        LastWriteTimeUtc = $beforeWrite.ToString('o')
        Sha256 = $hash
        Category = $category
    })
    $totalBytes += [uint64]$beforeLength
}
Write-Progress -Activity 'Hashing Condemned retail baseline' -Completed

$afterSnapshots = @(
    Get-ChildItem -LiteralPath $root -Recurse -File -Force |
        Sort-Object FullName
)
if ($afterSnapshots.Count -ne $fileSnapshots.Count) {
    throw 'Retail file count changed while the baseline manifest was captured.'
}
for ($i = 0; $i -lt $fileSnapshots.Count; $i++) {
    if ($fileSnapshots[$i].FullName -ne $afterSnapshots[$i].FullName -or
        $fileSnapshots[$i].Length -ne $afterSnapshots[$i].Length -or
        $fileSnapshots[$i].LastWriteTimeUtc -ne
            $afterSnapshots[$i].LastWriteTimeUtc) {
        throw ("Retail tree changed while the baseline manifest was captured: " +
            $fileSnapshots[$i].FullName)
    }
}

$commit = $null
$dirty = $null
try {
    $commit = (& git -C $cfg.ProjectRoot rev-parse HEAD 2>$null).Trim()
    $dirty = @(& git -C $cfg.ProjectRoot status --porcelain 2>$null).Count -gt 0
} catch {
    $commit = $null
    $dirty = $null
}

$manifest = [pscustomobject][ordered]@{
    SchemaVersion = 1
    GeneratedAtUtc = [DateTime]::UtcNow.ToString('o')
    Generator = 'tools\verify-condemned-m0.ps1'
    Project = [pscustomobject][ordered]@{
        Commit = $commit
        WorkingTreeDirty = $dirty
    }
    RetailRoot = $root
    RetailWasReadOnly = $true
    Steam = $installation.AppManifest
    ExpectedVersion = $cfg.ExpectedVersion
    VerifiedBuild = $allOk
    CriticalFiles = $criticalResults
    ExternalFixReference = [pscustomobject][ordered]@{
        Files = $thirdPartyResults
        Settings = $widescreenSettings
    }
    Inventory = [pscustomobject][ordered]@{
        FileCount = $inventory.Count
        TotalBytes = $totalBytes
        Files = $inventory
    }
}

$json = $manifest | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText(
    $output,
    $json + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Info 'Manifest' $output
Write-Info 'Files captured' ([string]$inventory.Count)
Write-Info 'Bytes captured' ([string]$totalBytes)
Write-Host ("=== Result: {0} ===" -f
    $(if ($allOk) { 'verified baseline captured' } else {
        'manifest captured, identity mismatch'
    })) -ForegroundColor $(if ($allOk) { 'Green' } else { 'Red' })

if (-not $allOk) {
    exit 1
}
exit 0
