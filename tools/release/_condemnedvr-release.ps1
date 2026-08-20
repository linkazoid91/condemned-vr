# Shared helpers for the redistributable Condemned VR package.
# Retail files are verified and copied from the user's own installation.

if (-not (Get-Command Get-CondemnedVrConfig -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot '_condemnedvr-env.ps1')
}

$script:CondemnedVrRelease = [ordered]@{
    Product = 'Condemned VR'
    ProductId = 'CondemnedVR'
    InstallSchemaVersion = 1
    PackageSchemaVersion = 1
    DefaultInstallDirectory = (Join-Path $env:USERPROFILE 'CondemnedVR')
    InstallManifestName = 'condemnedvr-install.json'
    StageDeploymentRelativePath =
        'stage\condemned-m2-mono\m2-mono-deployment.json'
    SteamVrManifest =
        'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
    VdxrManifest =
        'C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json'
}

function Get-CondemnedVrReleaseConfig {
    return $script:CondemnedVrRelease
}

function Get-NormalizedDirectoryPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'A directory path is required.'
    }
    return [IO.Path]::GetFullPath(
        $Path.Trim().Trim([char]34)).TrimEnd([char]92)
}

function Test-PathInside([string]$Candidate, [string]$Root) {
    $candidateFull = Get-NormalizedDirectoryPath $Candidate
    $rootFull = Get-NormalizedDirectoryPath $Root
    if ($candidateFull.Equals(
            $rootFull,
            [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $candidateFull.StartsWith(
        $rootFull + [char]92,
        [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-SafeRelativePath(
    [string]$Root,
    [string]$RelativePath
) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath)) {
        throw ('Unsafe package-relative path: {0}' -f $RelativePath)
    }
    $rootFull = Get-NormalizedDirectoryPath $Root
    $resolved = [IO.Path]::GetFullPath((Join-Path $rootFull $RelativePath))
    if (-not (Test-PathInside $resolved $rootFull)) {
        throw ('Package path escapes its root: {0}' -f $RelativePath)
    }
    return $resolved
}

function Assert-SafeCondemnedVrInstallRoot(
    [string]$InstallDir,
    [string]$RetailRoot,
    [string]$PackageRoot
) {
    $installFull = Get-NormalizedDirectoryPath $InstallDir
    $driveRoot = [IO.Path]::GetPathRoot($installFull).TrimEnd([char]92)
    if ($installFull.Equals(
            $driveRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The root of a drive cannot be used as the install directory.'
    }
    if (-not [string]::IsNullOrWhiteSpace($RetailRoot)) {
        $retailFull = Get-NormalizedDirectoryPath $RetailRoot
        if ((Test-PathInside $installFull $retailFull) -or
            (Test-PathInside $retailFull $installFull)) {
            throw 'The Condemned VR install and Retail directories must not overlap.'
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($PackageRoot)) {
        $packageFull = Get-NormalizedDirectoryPath $PackageRoot
        if ((Test-PathInside $installFull $packageFull) -or
            (Test-PathInside $packageFull $installFull)) {
            throw 'The extracted package and install directories must not overlap.'
        }
    }
    return $installFull
}

function Assert-CondemnedRetailReleaseIdentity([string]$RetailRoot) {
    $cfg = Get-CondemnedVrConfig
    $installation = Resolve-CondemnedRetailInstallation $RetailRoot
    $root = Get-NormalizedDirectoryPath $installation.RetailRoot
    foreach ($relativePath in $cfg.CriticalFiles.Keys) {
        $path = Join-Path $root $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw ('Required Retail file is missing: {0}' -f $path)
        }
        $actual = Get-FileSha256 $path
        $expected = $cfg.CriticalFiles[$relativePath].Sha256
        if ($actual -ne $expected) {
            throw (
                'Unsupported Retail file {0}. Expected SHA-256 {1}, found {2}.' -f
                $relativePath, $expected, $actual)
        }
    }
    $exe = Join-Path $root 'Condemned.exe'
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe).FileVersion
    if ($version -ne $cfg.ExpectedVersion) {
        throw (
            'Unsupported Condemned.exe version {0}; {1} is required.' -f
            $version, $cfg.ExpectedVersion)
    }
    return [pscustomobject][ordered]@{
        RetailRoot = $root
        RuntimeExe = $exe
        RuntimeSha256 = Get-FileSha256 $exe
        Version = $version
        SteamLibrary = $installation.SteamLibrary
        AppManifest = $installation.AppManifest
    }
}

function Test-CondemnedVrReleasePackage([string]$PackageRoot) {
    $releaseCfg = Get-CondemnedVrReleaseConfig
    $root = Get-NormalizedDirectoryPath $PackageRoot
    $manifestPath = Join-Path $root 'release-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw ('Release manifest is missing: {0}' -f $manifestPath)
    }
    try {
        $manifest = Get-Content -Raw -LiteralPath $manifestPath |
            ConvertFrom-Json
    } catch {
        throw ('Release manifest is invalid JSON: {0}' -f $manifestPath)
    }
    if ($manifest.product -ne $releaseCfg.Product -or
        [int]$manifest.schemaVersion -ne
            $releaseCfg.PackageSchemaVersion) {
        throw 'This is not a supported Condemned VR release package.'
    }
    if ([bool]$manifest.containsRetailContent) {
        throw 'Safety stop: the release manifest declares Retail content.'
    }
    $seen = @{}
    foreach ($entry in @($manifest.files)) {
        $relative = [string]$entry.path
        $key = $relative.ToLowerInvariant()
        if ($seen.ContainsKey($key)) {
            throw ('Duplicate release-manifest path: {0}' -f $relative)
        }
        $seen[$key] = $true
        $path = Resolve-SafeRelativePath $root $relative
        $actual = Get-FileSha256 $path
        if ($actual -ne [string]$entry.sha256) {
            throw ('Package file is missing or changed: {0}' -f $relative)
        }
        if ((Get-Item -LiteralPath $path).Length -ne [long]$entry.bytes) {
            throw ('Package file length changed: {0}' -f $relative)
        }
    }
    return $manifest
}

function Get-OpenXrRuntimeName([string]$ManifestPath) {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        return $null
    }
    try {
        return ([IO.File]::ReadAllText($ManifestPath) |
            ConvertFrom-Json).runtime.name
    } catch {
        return $null
    }
}

function Resolve-CondemnedVrOpenXrRuntime([string]$Runtime) {
    $releaseCfg = Get-CondemnedVrReleaseConfig
    if ([string]::IsNullOrWhiteSpace($Runtime) -or $Runtime -eq 'active') {
        $active = $null
        try {
            $active = (Get-ItemProperty (
                'HKLM:\SOFTWARE\Khronos\OpenXR\1') -ErrorAction Stop).
                ActiveRuntime
        } catch { }
        if ([string]::IsNullOrWhiteSpace($active) -or
            -not (Test-Path -LiteralPath $active -PathType Leaf)) {
            throw (
                'No active x64 OpenXR runtime was found. Configure SteamVR ' +
                'or Virtual Desktop as active, or use -Runtime.')
        }
        return [pscustomobject][ordered]@{
            Name = Get-OpenXrRuntimeName $active
            Manifest = $null
            ActiveManifest = [IO.Path]::GetFullPath($active)
            Override = $false
        }
    }
    $manifest = switch ($Runtime.ToLowerInvariant()) {
        'steamvr' { $releaseCfg.SteamVrManifest }
        'vdxr' { $releaseCfg.VdxrManifest }
        default { $Runtime }
    }
    $manifest = [IO.Path]::GetFullPath(
        $manifest.Trim().Trim([char]34))
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw ('OpenXR runtime manifest not found: {0}' -f $manifest)
    }
    return [pscustomobject][ordered]@{
        Name = Get-OpenXrRuntimeName $manifest
        Manifest = $manifest
        ActiveManifest = $null
        Override = $true
    }
}

function Read-CondemnedVrInstallManifest([string]$InstallDir) {
    $releaseCfg = Get-CondemnedVrReleaseConfig
    $root = Get-NormalizedDirectoryPath $InstallDir
    $path = Join-Path $root $releaseCfg.InstallManifestName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw ('No Condemned VR installation was found in: {0}' -f $root)
    }
    try {
        $manifest = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
    } catch {
        throw ('The installation manifest is invalid: {0}' -f $path)
    }
    if ($manifest.productId -ne $releaseCfg.ProductId -or
        [int]$manifest.schemaVersion -ne
            $releaseCfg.InstallSchemaVersion) {
        throw (
            'The directory is not a supported Condemned VR installation: {0}' -f
            $root)
    }
    if (-not (Get-NormalizedDirectoryPath $manifest.installRoot).Equals(
            $root,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The installation manifest belongs to a different directory.'
    }
    return $manifest
}

function Test-CondemnedVrInstalledDeployment([string]$InstallDir) {
    $root = Get-NormalizedDirectoryPath $InstallDir
    $install = Read-CondemnedVrInstallManifest $root
    $retail = Assert-CondemnedRetailReleaseIdentity $install.retailRoot
    foreach ($entry in @($install.installedFiles)) {
        $path = Resolve-SafeRelativePath $root ([string]$entry.path)
        if ((Get-FileSha256 $path) -ne [string]$entry.sha256) {
            throw (
                'Installed package file is missing or changed: {0}' -f
                $entry.path)
        }
    }
    $deploymentPath = Resolve-SafeRelativePath $root (
        [string]$install.stageDeployment)
    if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
        throw ('Stage deployment is missing: {0}' -f $deploymentPath)
    }
    $deployment = Get-Content -Raw -LiteralPath $deploymentPath |
        ConvertFrom-Json
    if ((Get-FileSha256 $deployment.ArchiveConfig) -ne
        [string]$deployment.ArchiveConfigSha256) {
        throw 'The installed archive configuration changed.'
    }
    if ((Get-FileSha256 $deployment.HostExe) -ne
        [string]$deployment.HostSha256) {
        throw 'The installed OpenXR host changed.'
    }
    foreach ($record in @($deployment.Files)) {
        $path = Join-Path $deployment.ModuleDirectory $record.Name
        if ((Get-FileSha256 $path) -ne [string]$record.Sha256) {
            throw (
                'Installed stage module is missing or changed: {0}' -f
                $record.Name)
        }
    }
    if ((Get-FileSha256 $deployment.RuntimeExe) -ne
        [string]$retail.RuntimeSha256) {
        throw 'The isolated Condemned.exe copy no longer matches Retail.'
    }
    return [pscustomobject][ordered]@{
        Install = $install
        Deployment = $deployment
        Retail = $retail
    }
}
