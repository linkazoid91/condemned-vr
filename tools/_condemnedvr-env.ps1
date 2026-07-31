# =============================================================================
# Shared Condemned VR M0 definitions and read-only helpers.
#
# Dot-source this file from development tools:
#   . "$PSScriptRoot\_condemnedvr-env.ps1"
#
# Functions in this file inspect Steam and retail files but do not modify them.
# =============================================================================

$script:CondemnedVr = [ordered]@{
    ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    SteamAppId = 4720
    SteamName = 'Condemned: Criminal Origins'
    InstallDirectoryName = 'Condemned Criminal Origins'
    ExpectedVersion = '1.0.314.0'
    CriticalFiles = [ordered]@{
        'Condemned.exe' = [ordered]@{
            Sha256 = '45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02'
            Machine = '0x014C'
            OptionalHeaderMagic = '0x010B'
            ImageBase = '0x00400000'
            SizeOfImage = '0x0018F000'
            Timestamp = '0x43FCFF00'
        }
        'Game\GameClient.dll' = [ordered]@{
            Sha256 = '0AC9798CA460C3E24EFC6D103D5FD258CCA6C921E0BD2A3FD9119D1C7C5228CC'
            Machine = '0x014C'
            OptionalHeaderMagic = '0x010B'
            ImageBase = '0x10000000'
            SizeOfImage = '0x00194000'
            Timestamp = '0x43FCFFDF'
        }
        'Game\GameServer.dll' = [ordered]@{
            Sha256 = '48321A894D47105707020ABF52A2B3CD2049D4366233C2CEB011240961AC26EC'
            Machine = '0x014C'
            OptionalHeaderMagic = '0x010B'
        }
    }
    KnownThirdPartyFiles = @(
        'd3d9.dll',
        'scripts\Condemned.WidescreenFix.asi',
        'scripts\Condemned.WidescreenFix.ini'
    )
}

function Get-CondemnedVrConfig {
    return $script:CondemnedVr
}

function Get-FileSha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    $fullPath = [IO.Path]::GetFullPath($Path)
    $stream = New-Object IO.FileStream(
        $fullPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read,
        1048576,
        [IO.FileOptions]::SequentialScan)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString(
            $sha256.ComputeHash($stream)).Replace('-', '')
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Assert-UnderCondemnedVrProjectRoot([string]$Path) {
    $root = [IO.Path]::GetFullPath((Get-CondemnedVrConfig).ProjectRoot)
    $full = [IO.Path]::GetFullPath($Path)
    $rootWithSeparator = $root.TrimEnd('\') + '\'
    if (-not $full.StartsWith(
            $rootWithSeparator,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Safety stop: '$full' is outside the project root '$root'."
    }
    return $full
}

function Get-PeIdentity([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PE file not found: $Path"
    }

    $stream = [IO.File]::Open(
        [IO.Path]::GetFullPath($Path),
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a DOS/PE image: $Path"
        }

        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt ($stream.Length - 24)) {
            throw "Invalid PE header offset in: $Path"
        }

        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Missing PE signature in: $Path"
        }

        $machine = $reader.ReadUInt16()
        $sectionCount = $reader.ReadUInt16()
        $timestamp = $reader.ReadUInt32()
        $reader.ReadUInt32() | Out-Null
        $reader.ReadUInt32() | Out-Null
        $optionalHeaderSize = $reader.ReadUInt16()
        $characteristics = $reader.ReadUInt16()
        $optionalHeaderOffset = $stream.Position

        if ($optionalHeaderSize -lt 60 -or
            ($optionalHeaderOffset + $optionalHeaderSize) -gt $stream.Length) {
            throw "Invalid optional PE header in: $Path"
        }

        $magic = $reader.ReadUInt16()
        if ($magic -eq 0x010B) {
            $stream.Position = $optionalHeaderOffset + 28
            $imageBase = [uint64]$reader.ReadUInt32()
        } elseif ($magic -eq 0x020B) {
            $stream.Position = $optionalHeaderOffset + 24
            $imageBase = $reader.ReadUInt64()
        } else {
            throw ("Unknown optional PE header magic 0x{0:X4} in: {1}" -f
                $magic, $Path)
        }

        $stream.Position = $optionalHeaderOffset + 56
        $sizeOfImage = $reader.ReadUInt32()

        return [pscustomobject][ordered]@{
            Machine = '0x{0:X4}' -f $machine
            OptionalHeaderMagic = '0x{0:X4}' -f $magic
            ImageBase = if ($magic -eq 0x010B) {
                '0x{0:X8}' -f $imageBase
            } else {
                '0x{0:X16}' -f $imageBase
            }
            SizeOfImage = '0x{0:X8}' -f $sizeOfImage
            Timestamp = '0x{0:X8}' -f $timestamp
            SectionCount = $sectionCount
            Characteristics = '0x{0:X4}' -f $characteristics
        }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Get-VdfString([string]$Text, [string]$Name) {
    $escapedName = [Text.RegularExpressions.Regex]::Escape($Name)
    $match = [Text.RegularExpressions.Regex]::Match(
        $Text,
        "(?im)^\s*`"$escapedName`"\s*`"([^`"]*)`"")
    if (-not $match.Success) {
        return $null
    }
    return $match.Groups[1].Value -replace '\\\\', '\'
}

function Read-SteamAppManifest([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    $text = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path))
    return [pscustomobject][ordered]@{
        Path = [IO.Path]::GetFullPath($Path)
        AppId = Get-VdfString $text 'appid'
        Name = Get-VdfString $text 'name'
        InstallDir = Get-VdfString $text 'installdir'
        BuildId = Get-VdfString $text 'buildid'
        StateFlags = Get-VdfString $text 'StateFlags'
        SizeOnDisk = Get-VdfString $text 'SizeOnDisk'
        Sha256 = Get-FileSha256 $Path
    }
}

function Get-SteamRoots {
    $roots = New-Object Collections.Generic.List[string]
    foreach ($key in @(
        'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
        'HKLM:\SOFTWARE\Valve\Steam',
        'HKCU:\SOFTWARE\Valve\Steam'
    )) {
        try {
            $value = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
        } catch {
            continue
        }
        foreach ($name in @('InstallPath', 'SteamPath')) {
            if ($value.PSObject.Properties.Name -contains $name -and
                -not [string]::IsNullOrWhiteSpace([string]$value.$name)) {
                $roots.Add([IO.Path]::GetFullPath([string]$value.$name))
            }
        }
    }

    $defaultRoot = Join-Path ${env:ProgramFiles(x86)} 'Steam'
    if (Test-Path -LiteralPath $defaultRoot -PathType Container) {
        $roots.Add([IO.Path]::GetFullPath($defaultRoot))
    }

    return @($roots | Select-Object -Unique)
}

function Get-SteamLibraryRoots {
    $libraries = New-Object Collections.Generic.List[string]
    foreach ($steamRoot in (Get-SteamRoots)) {
        $libraries.Add($steamRoot)
        $libraryFile = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraryFile -PathType Leaf)) {
            continue
        }
        $text = [IO.File]::ReadAllText($libraryFile)
        foreach ($match in [Text.RegularExpressions.Regex]::Matches(
                $text, '"path"\s*"([^"]+)"')) {
            $library = $match.Groups[1].Value -replace '\\\\', '\'
            if (-not [string]::IsNullOrWhiteSpace($library)) {
                $libraries.Add([IO.Path]::GetFullPath($library))
            }
        }
    }
    return @($libraries | Select-Object -Unique)
}

function Find-CondemnedRetailInstallation {
    $cfg = Get-CondemnedVrConfig
    $fallback = $null

    foreach ($library in (Get-SteamLibraryRoots)) {
        $manifestPath = Join-Path $library (
            "steamapps\appmanifest_$($cfg.SteamAppId).acf")
        $app = Read-SteamAppManifest $manifestPath
        if ($null -eq $app) {
            continue
        }

        $installDir = if ([string]::IsNullOrWhiteSpace($app.InstallDir)) {
            $cfg.InstallDirectoryName
        } else {
            $app.InstallDir
        }
        $retailRoot = Join-Path $library "steamapps\common\$installDir"
        $exe = Join-Path $retailRoot 'Condemned.exe'
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            continue
        }

        $candidate = [pscustomobject][ordered]@{
            RetailRoot = [IO.Path]::GetFullPath($retailRoot)
            SteamLibrary = [IO.Path]::GetFullPath($library)
            AppManifest = $app
        }
        if ((Get-FileSha256 $exe) -eq
            $cfg.CriticalFiles['Condemned.exe'].Sha256) {
            return $candidate
        }
        if ($null -eq $fallback) {
            $fallback = $candidate
        }
    }

    return $fallback
}

function Resolve-CondemnedRetailInstallation([string]$RetailRoot) {
    $cfg = Get-CondemnedVrConfig
    if ([string]::IsNullOrWhiteSpace($RetailRoot)) {
        $found = Find-CondemnedRetailInstallation
        if ($null -eq $found) {
            throw ("Condemned was not found in the configured Steam libraries. " +
                "Pass -RetailRoot explicitly.")
        }
        return $found
    }

    $fullRoot = [IO.Path]::GetFullPath($RetailRoot.Trim().Trim('"'))
    $exe = Join-Path $fullRoot 'Condemned.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Condemned.exe not found below: $fullRoot"
    }

    $app = $null
    $steamLibrary = $null
    foreach ($library in (Get-SteamLibraryRoots)) {
        $commonRoot = [IO.Path]::GetFullPath(
            (Join-Path $library 'steamapps\common')).TrimEnd('\') + '\'
        if (-not $fullRoot.StartsWith(
                $commonRoot,
                [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $candidateManifest = Join-Path $library (
            "steamapps\appmanifest_$($cfg.SteamAppId).acf")
        $app = Read-SteamAppManifest $candidateManifest
        $steamLibrary = $library
        break
    }

    return [pscustomobject][ordered]@{
        RetailRoot = $fullRoot
        SteamLibrary = $steamLibrary
        AppManifest = $app
    }
}

function Get-IniSettings([string]$Path) {
    $settings = [ordered]@{}
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $settings
    }

    foreach ($line in [IO.File]::ReadAllLines([IO.Path]::GetFullPath($Path))) {
        $withoutComment = ($line -split '//', 2)[0].Trim()
        if ([string]::IsNullOrWhiteSpace($withoutComment) -or
            $withoutComment.StartsWith('[')) {
            continue
        }
        $parts = $withoutComment -split '=', 2
        if ($parts.Count -eq 2) {
            $settings[$parts[0].Trim()] = $parts[1].Trim()
        }
    }
    return $settings
}
