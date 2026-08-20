<#
.SYNOPSIS
    Audits the repository before it is made public or packaged.

.DESCRIPTION
    Checks required notices, tracked paths, complete reachable Git history,
    large historical blobs and common credential signatures. The audit is
    deliberately conservative and performs no writes.

.PARAMETER RequireClean
    Fails instead of warning when the working tree is dirty.
#>
[CmdletBinding()]
param([switch]$RequireClean)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$failures = New-Object Collections.Generic.List[string]
$warnings = New-Object Collections.Generic.List[string]

function Invoke-GitLines {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $output = @(& git -C $projectRoot @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
    return $output
}

function Test-ForbiddenPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Scope
    )

    $normalized = $Path -replace '\\', '/'
    if ($normalized -match
        '(?i)^(vendor-local|build|stage|logs|dist|local-runtime)/') {
        $failures.Add("$Scope contains ignored local/runtime path: $Path")
    }
    if ($normalized -match
        '(?i)\.(exe|dll|pdb|arch00|rez|fxd|dtx|ltb|bik|zip|7z|rar)$') {
        $failures.Add("$Scope contains a forbidden binary/game path: $Path")
    }
}

foreach ($required in @(
        'LICENSE',
        'ATTRIBUTION.md',
        'THIRD_PARTY_NOTICES.md',
        'README.md')) {
    $requiredPath = Join-Path $projectRoot $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        $failures.Add("Required publication file is missing: $required")
    }
}

$licensePath = Join-Path $projectRoot 'LICENSE'
if (Test-Path -LiteralPath $licensePath -PathType Leaf) {
    $licenseText = [IO.File]::ReadAllText($licensePath)
    foreach ($requiredText in @(
            'Copyright (c) 2026 F.E.A.R. VR contributors',
            'Copyright (c) 2026 Condemned VR contributors',
            'Permission is hereby granted, free of charge')) {
        if (-not $licenseText.Contains($requiredText)) {
            $failures.Add("LICENSE is missing required text: $requiredText")
        }
    }
}

$attributionPath = Join-Path $projectRoot 'ATTRIBUTION.md'
if (Test-Path -LiteralPath $attributionPath -PathType Leaf) {
    $attribution = [IO.File]::ReadAllText($attributionPath)
    foreach ($requiredText in @(
            'https://github.com/DR-89/fear-vr',
            '24a6e22f20a02e64aa0955738f1050357b265400')) {
        if (-not $attribution.Contains($requiredText)) {
            $failures.Add("ATTRIBUTION.md is missing: $requiredText")
        }
    }
}

$thirdPartyNoticesPath = Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'
if (Test-Path -LiteralPath $thirdPartyNoticesPath -PathType Leaf) {
    $thirdPartyNotices = [IO.File]::ReadAllText($thirdPartyNoticesPath)
    foreach ($requiredText in @(
            'https://freesound.org/people/Nanashi/sounds/104409/',
            'https://freesound.org/people/vabadus/sounds/151067/',
            'https://creativecommons.org/publicdomain/zero/1.0/',
            'DDC9920E64C99E0F75DAED6B5F3D6B3DDB13933C12A4E0631D77109ECAF1FC42',
            '028A7976EBC5B629F944C2AF3126296E4CDC19512DE9F09829D41209CEF7485E')) {
        if (-not $thirdPartyNotices.Contains($requiredText)) {
            $failures.Add((
                'THIRD_PARTY_NOTICES.md is missing: {0}' -f $requiredText))
        }
    }
}

$trackedPaths = @(Invoke-GitLines -Arguments @('ls-files'))
foreach ($path in $trackedPaths) {
    Test-ForbiddenPath -Path $path -Scope 'Current index'
}

# Check paths from every reachable commit because the intended publication
# preserves the upstream history instead of squashing it.
$historyObjects = @(Invoke-GitLines -Arguments @(
    'rev-list', '--objects', '--all'))
foreach ($line in $historyObjects) {
    $separator = $line.IndexOf(' ')
    if ($separator -lt 0) {
        continue
    }
    $path = $line.Substring($separator + 1)
    if (-not [string]::IsNullOrWhiteSpace($path)) {
        Test-ForbiddenPath -Path $path -Scope 'Git history'
    }
}

# A source-only repository should not have unexpectedly large historical
# blobs. Five MiB is intentionally well above the largest current source file.
$batchRecords = @($historyObjects |
    & git -C $projectRoot cat-file `
        '--batch-check=%(objecttype) %(objectsize) %(rest)')
if ($LASTEXITCODE -ne 0) {
    throw 'git cat-file history scan failed.'
}
foreach ($record in $batchRecords) {
    if ($record -notmatch '^blob\s+(\d+)\s*(.*)$') {
        continue
    }
    $size = [int64]$Matches[1]
    $path = $Matches[2]
    if ($size -gt 5MB) {
        $failures.Add(
            "Git history contains a blob larger than 5 MiB: $path ($size bytes)")
    }
}

$baselineCommit = '24a6e22f20a02e64aa0955738f1050357b265400'
& git -C $projectRoot cat-file -e "$baselineCommit`^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) {
    $failures.Add(
        "The attributed F.E.A.R. VR baseline commit is not in Git history.")
}

# Scan current tracked text for high-signal credential formats. Generic words
# such as "password" are not treated as secrets because documentation may use
# them legitimately.
$secretPattern = [regex](
    '(?i)(github_pat_[A-Za-z0-9_]{20,}|ghp_[A-Za-z0-9]{30,}|' +
    'AKIA[0-9A-Z]{16}|-----BEGIN (RSA |OPENSSH |EC )?PRIVATE KEY-----|' +
    'sk-[A-Za-z0-9]{32,})')
foreach ($path in $trackedPaths) {
    $fullPath = Join-Path $projectRoot ($path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        continue
    }
    $item = Get-Item -LiteralPath $fullPath
    if ($item.Length -gt 2MB) {
        continue
    }
    $bytes = [IO.File]::ReadAllBytes($fullPath)
    if ($bytes -contains 0) {
        continue
    }
    $text = [Text.Encoding]::UTF8.GetString($bytes)
    if ($secretPattern.IsMatch($text)) {
        $failures.Add("Possible credential in tracked text: $path")
    }
}

$dirty = @(Invoke-GitLines -Arguments @('status', '--porcelain=v1'))
if ($dirty.Count -gt 0) {
    if ($RequireClean) {
        $failures.Add('The working tree is not clean.')
    } else {
        $warnings.Add(
            'The working tree is not clean; rerun with -RequireClean before publishing.')
    }
}

foreach ($warning in $warnings) {
    Write-Warning $warning
}
if ($failures.Count -gt 0) {
    Write-Host 'Publication audit failed:' -ForegroundColor Red
    foreach ($failure in ($failures | Select-Object -Unique)) {
        Write-Host "  - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host 'Publication audit passed.' -ForegroundColor Green
Write-Host "  Tracked paths:       $($trackedPaths.Count)"
Write-Host "  Historical objects:  $($historyObjects.Count)"
Write-Host '  Required notices:    present'
Write-Host '  Upstream baseline:   present'
Write-Host '  Forbidden paths:     none'
Write-Host '  Credential patterns: none'
