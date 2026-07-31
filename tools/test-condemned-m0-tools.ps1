<#
.SYNOPSIS
    Self-contained tests for the Condemned M0 PowerShell helpers.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"

$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'condemned-vr-m0-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

function Assert-Equal($Expected, $Actual, [string]$Label) {
    if ($Expected -ne $Actual) {
        throw "$Label expected '$Expected', got '$Actual'."
    }
}

try {
    $manifestPath = Join-Path $testRoot 'appmanifest_4720.acf'
    $manifestText = @'
"AppState"
{
    "appid" "4720"
    "name" "Condemned: Criminal Origins"
    "installdir" "Condemned Criminal Origins"
    "buildid" "15838"
    "StateFlags" "4"
    "SizeOnDisk" "6726781771"
}
'@
    [IO.File]::WriteAllText(
        $manifestPath,
        $manifestText,
        (New-Object Text.UTF8Encoding($false)))
    $manifest = Read-SteamAppManifest $manifestPath
    Assert-Equal '4720' $manifest.AppId 'app ID'
    Assert-Equal 'Condemned Criminal Origins' $manifest.InstallDir 'install dir'
    Assert-Equal '15838' $manifest.BuildId 'build ID'

    $iniPath = Join-Path $testRoot 'widescreen.ini'
    [IO.File]::WriteAllText(
        $iniPath,
        "[MAIN]`r`nFixAspectRatio = 1 // comment`r`nFixMenu=0`r`n",
        (New-Object Text.UTF8Encoding($false)))
    $settings = Get-IniSettings $iniPath
    Assert-Equal '1' $settings.FixAspectRatio 'INI comment stripping'
    Assert-Equal '0' $settings.FixMenu 'INI compact assignment'

    $pePath = Join-Path $testRoot 'fixture.exe'
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [BitConverter]::GetBytes([uint32]0x80).CopyTo($bytes, 0x3C)
    [BitConverter]::GetBytes([uint32]0x00004550).CopyTo($bytes, 0x80)
    [BitConverter]::GetBytes([uint16]0x014C).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes([uint16]3).CopyTo($bytes, 0x86)
    [BitConverter]::GetBytes(
        [Convert]::ToUInt32('43FCFF00', 16)).CopyTo($bytes, 0x88)
    [BitConverter]::GetBytes([uint16]0x00E0).CopyTo($bytes, 0x94)
    [BitConverter]::GetBytes([uint16]0x010F).CopyTo($bytes, 0x96)
    [BitConverter]::GetBytes([uint16]0x010B).CopyTo($bytes, 0x98)
    [BitConverter]::GetBytes([uint32]0x00400000).CopyTo($bytes, 0xB4)
    [BitConverter]::GetBytes([uint32]0x0018F000).CopyTo($bytes, 0xD0)
    [IO.File]::WriteAllBytes($pePath, $bytes)

    $pe = Get-PeIdentity $pePath
    Assert-Equal '0x014C' $pe.Machine 'PE machine'
    Assert-Equal '0x010B' $pe.OptionalHeaderMagic 'PE32 magic'
    Assert-Equal '0x00400000' $pe.ImageBase 'PE image base'
    Assert-Equal '0x0018F000' $pe.SizeOfImage 'PE image size'
    Assert-Equal '0x43FCFF00' $pe.Timestamp 'PE timestamp'

    Write-Host 'Condemned M0 helper tests passed.' -ForegroundColor Green
} finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedTempRoot = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedTestRoot.StartsWith(
            $resolvedTempRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected test directory: $resolvedTestRoot"
    }
    if (Test-Path -LiteralPath $resolvedTestRoot -PathType Container) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
