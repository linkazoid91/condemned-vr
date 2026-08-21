function Resolve-CondemnedVrLaunchProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$BoundParameters
    )

    # Parameters outside this list tune the selected launch or request a
    # rollback; they do not replace the default feature platform. Keeping this
    # list explicit prevents a new diagnostic from being enabled implicitly.
    $featureParameters = @(
        'WeaponTest',
        'RendererProbe',
        'RendererPassThrough',
        'StereoDiagnostic',
        'DoubleRenderDiagnostic',
        'CameraReadProbe',
        'EyeOffsetDiagnostic',
        'ReverseEyeOffsetDiagnostic',
        'ZeroEyeOffsetDiagnostic',
        'StereoTuning',
        'LocomotionProbe',
        'TurningProbe',
        'MenuProbe',
        'MenuControlsProbe',
        'RetailVrSettingsProbe',
        'InteractionProbe',
        'CoreActionsProbe',
        'ForensicMemoryProbe',
        'HapticsProbe',
        'HeadAimProbe',
        'AimPathProbe',
        'MeleeAimProbe',
        'PhysicalMeleeProbe',
        'PhysicalMeleeWallProxy',
        'PhysicalMeleeContactDamage',
        'PhysicalMeleeColliderDebug',
        'PhysicalMeleeVisualProxy',
        'WeaponGripCalibration',
        'WeaponCatalogProbe',
        'ArmIkDiscovery',
        'ArmIkRightHandProof',
        'ArmIkRightArm',
        'TwoHandedMelee',
        'PerformanceProbe',
        'RecenterProbe',
        'DesktopWindow'
    )
    $explicitFeatures = @(
        $featureParameters | Where-Object {
            $BoundParameters.Keys -contains $_
        })
    $minimal = $BoundParameters.Keys -contains 'Minimal' -and
        [bool]$BoundParameters['Minimal']
    $retailHeadBob = $BoundParameters.Keys -contains 'RetailHeadBob' -and
        [bool]$BoundParameters['RetailHeadBob']
    if ($minimal -and $explicitFeatures.Count -ne 0) {
        throw ('-Minimal cannot be combined with feature-selection ' +
            'parameters: ' + ($explicitFeatures -join ', '))
    }
    if ($minimal) {
        return [pscustomobject][ordered]@{
            Name = 'Minimal'
            ApplyPipePreset = $false
            EnableRetailVrSettings = $false
            ExplicitFeatureSelection = $false
            RetailHeadBobSuppressed = $false
            RetailHeadBobCommandValue = $(
                if ($retailHeadBob) { 1 } else { $null })
        }
    }
    if ($explicitFeatures.Count -eq 0) {
        return [pscustomobject][ordered]@{
            Name = 'Current'
            ApplyPipePreset = $true
            EnableRetailVrSettings = $true
            ExplicitFeatureSelection = $false
            RetailHeadBobSuppressed = -not $retailHeadBob
            RetailHeadBobCommandValue = $(
                if ($retailHeadBob) { 1 } else { 0 })
        }
    }
    $explicitPipeOnly = $explicitFeatures.Count -eq 1 -and
        $explicitFeatures[0] -eq 'WeaponTest' -and
        [string]$BoundParameters['WeaponTest'] -eq 'Pipe'
    return [pscustomobject][ordered]@{
        Name = $(if ($explicitPipeOnly) { 'Pipe' } else { 'Custom' })
        ApplyPipePreset = $false
        EnableRetailVrSettings = $false
        ExplicitFeatureSelection = $true
        RetailHeadBobSuppressed = -not $retailHeadBob
        RetailHeadBobCommandValue = $(
            if ($retailHeadBob) { 1 } else { 0 })
    }
}

function Get-CondemnedVrRetailHeadBobArguments {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [psobject]$LaunchProfile
    )

    $commandValue = $LaunchProfile.RetailHeadBobCommandValue
    if ($null -eq $commandValue) {
        return
    }

    '+HeadBob'
    ([int]$commandValue).ToString(
        [Globalization.CultureInfo]::InvariantCulture)
    if ($LaunchProfile.RetailHeadBobSuppressed) {
        if ([int]$commandValue -eq 0) {
            '-condemnedvr-m5-retail-headbob-post-profile-zero'
        } else {
            '-condemnedvr-m5-retail-headbob-post-profile-one'
        }
    }
}

function Add-CondemnedVrRetailHeadBobArguments {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$GameArguments,

        [Parameter(Mandatory = $true)]
        [psobject]$LaunchProfile
    )

    $GameArguments
    Get-CondemnedVrRetailHeadBobArguments $LaunchProfile
}

function Assert-CondemnedVrHeadBobDiagnosticProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [psobject]$LaunchProfile,

        [Parameter(Mandatory = $true)]
        [bool]$HeadBobDiagnostic
    )

    if ($HeadBobDiagnostic -and $LaunchProfile.Name -eq 'Minimal') {
        throw '-HeadBobDiagnostic cannot be combined with -Minimal.'
    }
}
