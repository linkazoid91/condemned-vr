<#
.SYNOPSIS
    Emits native module information for a running 32-bit Condemned process.

.DESCRIPTION
    This script must run under 32-bit Windows PowerShell. A 64-bit .NET process
    can return only the WOW64 host modules when inspecting this legacy game.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId
)

$ErrorActionPreference = 'Stop'
if ([IntPtr]::Size -ne 4) {
    throw 'inspect-condemned-process32.ps1 must run under 32-bit PowerShell.'
}

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$modules = @($process.Modules | ForEach-Object {
    [pscustomobject][ordered]@{
        Name = $_.ModuleName
        Path = $_.FileName
    }
})

[pscustomobject][ordered]@{
    ProcessId = $process.Id
    Modules = $modules
} | ConvertTo-Json -Depth 5 -Compress
