<#
.SYNOPSIS
    Installs or updates Condemned VR in an isolated directory.

.DESCRIPTION
    Verifies the exact Steam 1.0.314.0 build, copies Retail inputs from the
    user's own installation, and stages project-authored VR modules without
    modifying the Steam tree. Updates preserve userdata and weapon settings.
#>
[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:USERPROFILE 'CondemnedVR'),
    [string]$RetailRoot,
    [switch]$NoShortcut,
    [switch]$NonInteractive
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-release.ps1')
$cfg = Get-CondemnedVrConfig
$releaseCfg = Get-CondemnedVrReleaseConfig
$packageRoot = Get-NormalizedDirectoryPath (Join-Path $PSScriptRoot '..')
$package = Test-CondemnedVrReleasePackage $packageRoot
$canPrompt = -not $NonInteractive -and [Environment]::UserInteractive

function Request-CondemnedRetailRoot([string]$Failure) {
    if (-not $canPrompt) {
        throw (
            $Failure + [Environment]::NewLine +
            'Re-run with -RetailRoot followed by the Condemned folder.')
    }
    Write-Host ''
    Write-Host 'Condemned was not found or is unsupported.' -ForegroundColor Yellow
    Write-Host $Failure
    Write-Host 'Enter the folder containing Condemned.exe.'
    Write-Host (
        'Example: D:\SteamLibrary\steamapps\common\' +
        'Condemned Criminal Origins')
    while ($true) {
        $answer = Read-Host 'Path (empty to abort)'
        if ([string]::IsNullOrWhiteSpace($answer)) {
            throw 'Installation aborted.'
        }
        try {
            return Assert-CondemnedRetailReleaseIdentity $answer
        } catch {
            Write-Host $_.Exception.Message -ForegroundColor Yellow
        }
    }
}

$previous = $null
$candidateInstall = Get-NormalizedDirectoryPath $InstallDir
$previousPath = Join-Path $candidateInstall $releaseCfg.InstallManifestName
if (Test-Path -LiteralPath $previousPath -PathType Leaf) {
    $previous = Read-CondemnedVrInstallManifest $candidateInstall
    if ([string]::IsNullOrWhiteSpace($RetailRoot)) {
        $RetailRoot = [string]$previous.retailRoot
    }
}

$retail = $null
try {
    $retail = Assert-CondemnedRetailReleaseIdentity $RetailRoot
} catch {
    $retail = Request-CondemnedRetailRoot $_.Exception.Message
}
$InstallDir = Assert-SafeCondemnedVrInstallRoot (
    $candidateInstall) $retail.RetailRoot $packageRoot

$runningGame = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
$runningHost = @(
    Get-Process -Name 'condemnedvr-host' -ErrorAction SilentlyContinue)
if ($runningGame.Count -gt 0 -or $runningHost.Count -gt 0) {
    throw 'Close Condemned and the Condemned VR host before installing.'
}

$operation = if ($null -ne $previous) { 'Update' } else { 'Installation' }
Write-Host ('=== Condemned VR - {0} ===' -f $operation) -ForegroundColor Cyan
Write-Host ('Package: {0}' -f $package.version)
Write-Host ('Retail:  {0}' -f $retail.RetailRoot)
Write-Host ('Target:  {0}' -f $InstallDir)

# Copy the verified project-authored package into the isolated install root.
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$installedRecords = New-Object Collections.Generic.List[object]
$newInstalledKeys = @{}
foreach ($entry in @($package.files)) {
    $relative = [string]$entry.path
    if ($relative -ieq 'Install.cmd') {
        continue
    }
    $source = Resolve-SafeRelativePath $packageRoot $relative
    $destination = Resolve-SafeRelativePath $InstallDir $relative
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $actual = Get-FileSha256 $destination
    if ($actual -ne [string]$entry.sha256) {
        throw ('Installed package copy failed verification: {0}' -f $relative)
    }
    $newInstalledKeys[$relative.ToLowerInvariant()] = $true
    $installedRecords.Add([pscustomobject][ordered]@{
        path = $relative
        sha256 = $actual
        bytes = (Get-Item -LiteralPath $destination).Length
    })
}

$packageManifestSource = Join-Path $packageRoot 'release-manifest.json'
$packageManifestTarget = Join-Path $InstallDir 'release-manifest.json'
Copy-Item -LiteralPath $packageManifestSource -Destination (
    $packageManifestTarget) -Force
$newInstalledKeys['release-manifest.json'] = $true
$installedRecords.Add([pscustomobject][ordered]@{
    path = 'release-manifest.json'
    sha256 = Get-FileSha256 $packageManifestTarget
    bytes = (Get-Item -LiteralPath $packageManifestTarget).Length
})

# Remove only stale files explicitly owned by the preceding installation.
if ($null -ne $previous) {
    foreach ($entry in @($previous.installedFiles)) {
        $relative = [string]$entry.path
        if ($newInstalledKeys.ContainsKey($relative.ToLowerInvariant())) {
            continue
        }
        $stale = Resolve-SafeRelativePath $InstallDir $relative
        if (Test-Path -LiteralPath $stale -PathType Leaf) {
            Remove-Item -LiteralPath $stale -Force
            Write-Host ('Removed stale package file: {0}' -f $relative)
        }
    }
}

# Build the read-only Retail runtime copy used by the current guarded launcher.
$stageRoot = Join-Path $InstallDir 'stage\condemned-m2-mono'
$runtimeStage = Join-Path $stageRoot 'retail-runtime'
$moduleDirectory = Join-Path $stageRoot 'game-override'
$userDirectory = Join-Path $InstallDir 'userdata'
$logDirectory = Join-Path $stageRoot 'logs'
foreach ($directory in @(
        $stageRoot, $runtimeStage, $moduleDirectory,
        $userDirectory, $logDirectory
    )) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$retailFileRecords = New-Object Collections.Generic.List[object]
$expectedRetailNames = @{}
$retailRootFiles = @(
    Get-ChildItem -LiteralPath $retail.RetailRoot -File -Force |
        Where-Object { $_.Name -ine 'd3d9.dll' } |
        Sort-Object Name)
foreach ($source in $retailRootFiles) {
    $destination = Join-Path $runtimeStage $source.Name
    Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
    $sourceHash = Get-FileSha256 $source.FullName
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw ('Retail stage copy failed verification: {0}' -f $source.Name)
    }
    $expectedRetailNames[$source.Name.ToLowerInvariant()] = $true
    $retailFileRecords.Add([pscustomobject][ordered]@{
        Name = $source.Name
        Length = $source.Length
        Sha256 = $destinationHash
    })
}

$previousRetailNames = @{}
if ($null -ne $previous) {
    foreach ($entry in @($previous.retailFiles)) {
        $previousRetailNames[([string]$entry.Name).ToLowerInvariant()] = $true
    }
}
foreach ($existing in @(
        Get-ChildItem -LiteralPath $runtimeStage -File -Force
    )) {
    $key = $existing.Name.ToLowerInvariant()
    if ($expectedRetailNames.ContainsKey($key)) {
        continue
    }
    if ($previousRetailNames.ContainsKey($key)) {
        Remove-Item -LiteralPath $existing.FullName -Force
        continue
    }
    throw ('Unexpected file in isolated Retail stage: {0}' -f $existing.FullName)
}

$retailGame = Join-Path $retail.RetailRoot 'Game'
$stageGame = Join-Path $runtimeStage 'Game'
if (Test-Path -LiteralPath $stageGame) {
    $gameItem = Get-Item -LiteralPath $stageGame -Force
    if (-not ($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw ('The isolated Game path is not a junction: {0}' -f $stageGame)
    }
    $junctionTarget = [string](@($gameItem.Target)[0])
    if (-not (Get-NormalizedDirectoryPath $junctionTarget).Equals(
            (Get-NormalizedDirectoryPath $retailGame),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The isolated Game junction points to a different Retail install.'
    }
} else {
    New-Item -ItemType Junction -Path $stageGame -Target $retailGame |
        Out-Null
}
foreach ($language in @(
        'English', 'French', 'German', 'Italian', 'Spanish'
    )) {
    New-Item -ItemType Directory -Force -Path (
        Join-Path $runtimeStage $language) | Out-Null
}
foreach ($forbidden in @('d3d9.dll', 'scripts')) {
    if (Test-Path -LiteralPath (Join-Path $runtimeStage $forbidden)) {
        throw ('Forbidden external-fix component entered the stage: {0}' -f
            $forbidden)
    }
}

# Stage the project loader/bridge and the user's verified stock client.
$moduleSources = [ordered]@{
    'GameClient.dll' =
        (Join-Path $InstallDir 'bin\x86\GameClient.dll')
    'condemnedvr-defaults.ini' =
        (Join-Path $InstallDir 'bin\x86\condemnedvr-defaults.ini')
    'GameOrig.dll' =
        (Join-Path $retail.RetailRoot 'Game\GameClient.dll')
    'condemnedvr-d3d9.dll' =
        (Join-Path $InstallDir 'bin\x86\condemnedvr-d3d9.dll')
}
$allowedModuleNames = @(
    'GameClient.dll',
    'condemnedvr-defaults.ini',
    'GameOrig.dll',
    'condemnedvr-d3d9.dll',
    'condemnedvr-loader.log')
foreach ($existing in @(
        Get-ChildItem -LiteralPath $moduleDirectory -Force
    )) {
    if ($allowedModuleNames -inotcontains $existing.Name) {
        throw ('Unexpected item in the VR module stage: {0}' -f
            $existing.FullName)
    }
}
$moduleRecords = New-Object Collections.Generic.List[object]
foreach ($name in $moduleSources.Keys) {
    $source = $moduleSources[$name]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw ('Required install input is missing: {0}' -f $source)
    }
    $destination = Join-Path $moduleDirectory $name
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $sourceHash = Get-FileSha256 $source
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw ('VR module stage copy failed verification: {0}' -f $name)
    }
    $moduleRecords.Add([pscustomobject][ordered]@{
        Name = $name
        Origin = if ($name -eq 'GameOrig.dll') { 'retail' } else { 'package' }
        Length = (Get-Item -LiteralPath $destination).Length
        Sha256 = $destinationHash
    })
}

$loaderLog = Join-Path $moduleDirectory 'condemnedvr-loader.log'
if (Test-Path -LiteralPath $loaderLog -PathType Leaf) {
    [IO.File]::WriteAllText(
        $loaderLog, '', (New-Object Text.UTF8Encoding($false)))
}

$archiveConfig = Join-Path $stageRoot 'm2-mono.archcfg'
$retailArchCfg = Join-Path $retail.RetailRoot 'default.archcfg'
$archiveLines = @(
    [IO.File]::ReadAllLines($retailArchCfg) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$archiveLines += $moduleDirectory
[IO.File]::WriteAllLines(
    $archiveConfig, $archiveLines, [Text.Encoding]::ASCII)

$hostExe = Join-Path $InstallDir 'bin\x64\condemnedvr-host.exe'
$stageDeploymentPath = Join-Path $InstallDir (
    $releaseCfg.StageDeploymentRelativePath)
$stageDeployment = [pscustomobject][ordered]@{
    SchemaVersion = 1
    Milestone = 'EndUser-Current'
    PreparedAtUtc = [DateTime]::UtcNow.ToString('o')
    RuntimeExe = Join-Path $runtimeStage 'Condemned.exe'
    WorkingDirectory = $runtimeStage
    RetailRoot = $retail.RetailRoot
    ModuleDirectory = $moduleDirectory
    UserDirectory = $userDirectory
    LogDirectory = $logDirectory
    ArchiveConfig = $archiveConfig
    ArchiveConfigSha256 = Get-FileSha256 $archiveConfig
    LoaderLog = $loaderLog
    HostExe = $hostExe
    HostSha256 = Get-FileSha256 $hostExe
    Files = $moduleRecords
    CaptureEnabled = $true
    OpenXrEnabled = $true
    StereoEnabled = $false
    AsiEnabled = $false
}
[IO.File]::WriteAllText(
    $stageDeploymentPath,
    ($stageDeployment | ConvertTo-Json -Depth 8) +
        [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

# Record ownership so updates and uninstall remove only installer-created data.
$shortcutPath = Join-Path (
    [Environment]::GetFolderPath('Desktop')) 'Condemned VR.lnk'
$installManifestPath = Join-Path $InstallDir (
    $releaseCfg.InstallManifestName)
$installManifest = [pscustomobject][ordered]@{
    schemaVersion = $releaseCfg.InstallSchemaVersion
    productId = $releaseCfg.ProductId
    product = $releaseCfg.Product
    installedUtc = [DateTime]::UtcNow.ToString('o')
    packageVersion = [string]$package.version
    packageGitCommit = [string]$package.gitCommit
    installRoot = $InstallDir
    retailRoot = $retail.RetailRoot
    retailVersion = $retail.Version
    runtimeSha256 = $retail.RuntimeSha256
    steamAppId = $cfg.SteamAppId
    stageDeployment = $releaseCfg.StageDeploymentRelativePath
    userDirectory = $userDirectory
    shortcut = $shortcutPath
    installedFiles = $installedRecords
    retailFiles = $retailFileRecords
}
[IO.File]::WriteAllText(
    $installManifestPath,
    ($installManifest | ConvertTo-Json -Depth 8) +
        [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

if (-not $NoShortcut) {
    $playScript = Join-Path $InstallDir 'tools\play.ps1'
    $powerShell = Join-Path $env:WINDIR (
        'System32\WindowsPowerShell\v1.0\powershell.exe')
    $quote = [char]34
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($shortcutPath)
    $link.TargetPath = $powerShell
    $link.Arguments = (
        '-NoProfile -ExecutionPolicy Bypass -File {0}{1}{0} ' +
        '-InstallDir {0}{2}{0}' -f
        $quote, $playScript, $InstallDir)
    $link.WorkingDirectory = $InstallDir
    $link.IconLocation = ('{0},0' -f $retail.RuntimeExe)
    $link.Description = 'Start Condemned VR'
    $link.Save()
    Write-Host ('Shortcut: {0}' -f $shortcutPath)
}

$retailAfter = Assert-CondemnedRetailReleaseIdentity $retail.RetailRoot
if ($retailAfter.RuntimeSha256 -ne $retail.RuntimeSha256) {
    throw 'Safety stop: the Retail executable changed during installation.'
}
$verified = Test-CondemnedVrInstalledDeployment $InstallDir
Write-Host ''
Write-Host ('Condemned VR {0} is ready.' -f $package.version) -ForegroundColor Green
Write-Host ('Launch: {0}' -f (Join-Path $InstallDir 'Play.cmd'))
Write-Host 'The Steam installation was not modified.'
